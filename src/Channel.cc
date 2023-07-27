#include "Channel.h"
#include "EventLoop.h"
#include "Logger.h"
#include <sys/epoll.h>


const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;

Channel::Channel(EventLoop* loop, int fd)
    :loop_(loop),fd_(fd),events_(0),revents_(0),index_(-1),tied_(false)
{}

Channel::~Channel()
{}


//防止TcpConnection被手动remove掉后channel还在执行TcpConnection绑定给channel的回调函数
// TcpConnection建立连接时，用一个weakptr指向一个TcpConnection，在调用回调函数的时候需要
// 先明确TcpConnection是否还存在，因为回调函数绑定了TcpConnection的this指针
void Channel::tie(const std::shared_ptr<void>& obj)
{
    tie_ = obj;//用弱ptr观察强(TcpConnection的shared_ptr)
    tied_ = true;
}

//当改变channel所表示的fd的events事件后，需要update在poller里面更改fd相应的事件epoll_ctl
void Channel::update()
{
    //通过channel所属的eventLoop ，调用poller的相应方法，注册fd的events事件
    loop_->updateChannel(this);
}

//在channel所属的EventLoop中把当前channel删除掉
void Channel::remove()
{
    loop_->removeChannel(this);
}

//fd得到poller通知以后，处理事件的函数，调用相应的回调函数
void Channel::handleEvent(Timestamp receiveTime)
{
    if(tied_)//在调用回调函数的时候需要先明确TcpConnection是否还存在，因为回调函数绑定了TcpConnection的this指针
    {
        std::shared_ptr<void> guard = tie_.lock();
        if(guard)
        {
            handleEventWithGuard(receiveTime);
        }//如果weakptr提升失败就说明tcpConnection已经没有了，就不用做回调了
    }
    else
    {
        handleEventWithGuard(receiveTime);
    }
}

//执行Poller通知的Channel发生的具体事件，由channel调用相应的回调操作
// 这些回调函数都是TcpConnection传进来的
void Channel::handleEventWithGuard(Timestamp receiveTime)
{
    LOG_INFO("channel handleEvent revents:%d\n",revents_);

    if((revents_&EPOLLHUP)&&!(revents_&EPOLLIN))//  sockfd会自动在poller中注册EPOLLHUP事件（sockfd关闭的事件）
    {
        if(closeCallback_)
        {
            closeCallback_();
        }
    }
    if(revents_ & EPOLLERR)
    {
        if(errorCallback_)
        {
            errorCallback_();
        }
    }
    if(revents_ & (EPOLLIN | EPOLLPRI))
    {
        if(readCallback_)
        {
            readCallback_(receiveTime);
        }
    }
    if(revents_ & EPOLLOUT)
    {
        if(writeCallback_)
        {
            writeCallback_();
        }
    }
}