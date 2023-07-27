#include "EventLoop.h"
#include "Logger.h"
#include "Poller.h"
#include "Channel.h"
#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <memory>

//防止一个线程创建多个EventLoop
__thread EventLoop* t_loopInThisThread = nullptr;  //thread_local每一个线程都有这个全局变量的副本

//定义默认的Poller Io复用接口的超时时间
const int kPollTimeMs = 100000;

//创建wakeupfd，用来notify唤醒subReactor处理新来的channel
int createEventfd()
{
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if(evtfd < 0)
    {
        LOG_FATAL("eventFd error:%d \n",errno);
    }
    return evtfd;
    //eventfd包含一个由内核维护的64位无符号整型计数器，创建eventfd时会返回一个文件描述符，
    //进程可以通过对这个文件描述符进行read/write来读取/改变计数器的值，从而实现进程间通信。
}

EventLoop::EventLoop()
    :looping_(false),
    quit_(false),
    callingPendingFunctors_(false),
    threadId_(CurrentThread::tid()),
    poller_(Poller::newDefaultPoller(this)),
    wakeupFd_(createEventfd()),
    wakeupChannel_(new Channel(this,wakeupFd_)),
    currentActiveChannel_(nullptr)
{
    LOG_DEBUG("EventLoop created %p in thread %d \n",this, threadId_);
    if(t_loopInThisThread)
    {
        LOG_FATAL("Another EventLoop %p exists in this thread %d \n",t_loopInThisThread, threadId_);
    }
    else
    {
        t_loopInThisThread = this;
    }

    //设置wakeupfd的事件类型以及发生事件后的回调操作
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead,this));
    //每一个EventLoop都将监听wakeupChannel的EPOLLIN读事件了
    wakeupChannel_->enableReading();//这里将新来的Channel提交到Poller，并唤醒Poller

}

EventLoop::~EventLoop()
{
    wakeupChannel_->disableAll();
    wakeupChannel_->remove();
    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;
}

void EventLoop::handleRead()    //单纯用来唤醒EventLoop的，为了将新连接分发给Loop，但是loop在等待旧连接用户的信息，需要通过eventfd写数据然后唤醒
{
    uint64_t one = 1;
    ssize_t n = read(wakeupFd_,&one,sizeof one);
    if(n!=sizeof one)
    {
        LOG_ERROR("EventLoop::handleRead() reads %d bytes instead of 8",n);
    }
}


//开启事件循环
void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;

    LOG_INFO("EventLoop %p start looping \n", this);
    while(!quit_)
    {
        activeChannels_.clear();
        //poller监听两类fd，一种是client的fd，一种是wakeupfd（mainLoop唤醒subLoop的fd（eventfd））
        pollReturnTime_ = poller_->poll(kPollTimeMs,&activeChannels_);
        for(Channel* channel : activeChannels_)
        {
            //poller监听哪些channel发生事件了，然后上报给EventLoop，通知channel处理相应的事件
            channel->handleEvent(pollReturnTime_);
        }

        //执行当前EventLoop事件循环需要处理的回调操作
        /**
         * IO线程 mainLoop->accept的工作，返回fd（channel打包），分发给subLoop
         * mainLoop事先注册一个回调cb（需要subLoop执行），
         * mainLoop 用eventfd去wakeup subLoop后执行下面的回调方法
         * （std::vector<Functor> pendingFunctors_里面的回调）
        */
        doPendingFunctors();
    }

    LOG_INFO("EventLoop %p stop looping!", this);
}
//退出事件循环,

void EventLoop::quit()
{
    quit_ = true;//1.loop在自己的线程中调用quit，

    if(!isInLoopThread())//2.如果是在其他线程调用quit，比如说在subloop（worker）中调用了mainloop（IO线程）的quit
        wakeup();//先唤醒，然后循环到while(!quit_)时退出
}


void EventLoop::runInLoop(Functor cb)
{
    // 在当前loop中执行callback，判断是否处于当前IO线程，是则执行这个函数，如果不是则将函数加入队列
    if(isInLoopThread())
        cb();
    else
    {
        queueInLoop(cb);//在非当前线程执行cb，就需要唤醒loop所在线程执行cb
    }
}

// cb放入队列中，唤醒loop所在的线程，执行callback
void EventLoop::queueInLoop(Functor cb)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(cb);
    }

    // 唤醒相应的，需要执行上面回调操作的loop线程
    // 当前loop的线程和loop不一致， 需要唤醒工作线程
    // 或者 callingPendingFunctors_==true 当前loop正在执行回调，没有阻塞在loop上，
    // 这时我需要在wakeupfd中写个1，让他执行完回调操作之后while循环再回到poller_->poll等待的时候 
    // 因为wakeupfd有数据可读就不会被阻塞，继续执行我刚刚新添加的cb
    if(!isInLoopThread() || callingPendingFunctors_)
    {
        wakeup();//唤醒loop所在线程
    }
}

// 用来唤醒loop所在线程的,向wakeupfd写一个数据,wakeupChannel就发生读事件，当前loop线程就会被唤醒
void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_,&one, sizeof one);
    if(n!=sizeof one)
    {
        LOG_ERROR("EventLoop::wakeup() writes %lu bytes instead of 8",n);
    }
}

// EventLoop方法就是去调用Poller的方法来修改Channel
void EventLoop::updateChannel(Channel* channel)
{
    poller_->updateChannel(channel);
}
void EventLoop::removeChannel(Channel* channel)
{
    poller_->removeChannel(channel);
}
void EventLoop::hasChannel(Channel* channel)
{
    poller_->hasChannel(channel);
}


// 执行回调  
void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);//先把pendingFunctors里面的回调拿出来，防止一直占用锁，导致queueInLoop中添加cb时阻塞
    }//释放锁，使锁的粒度最小
    
    //拿出来回调函数之后，然后去调用回调，
    for(const Functor& functor : functors)
    {
        functor();//执行当前loop需要执行的回调操作
    }

    callingPendingFunctors_ = false;
}
