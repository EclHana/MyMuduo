#pragma once
#include "noncopyable.h"
#include "Timestamp.h"
#include "CurrentThread.h"
#include <functional>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>
class Channel;
class Poller;


//事件循环类，主要包含两大模块，1是channel，2是Poller（epoll的抽象）
//相当于Reactor

class EventLoop : noncopyable
{
public:
    using Functor = std::function<void()>;
    EventLoop();
    ~EventLoop();

    //开启事件循环
    void loop();
    //退出事件循环
    void quit();

    Timestamp pollReturnTime() const { return pollReturnTime_; }

    // 在当前loop中执行callback，判断是否处于当前IO线程，是则执行这个函数，如果不是则将函数加入队列
    void runInLoop(Functor cb);
    // cb放入队列中，唤醒loop所在的线程，执行callback
    void queueInLoop(Functor cb);

    // 用来唤醒loop所在线程的
    void wakeup();

    // EventLoop方法就是去调用Poller的方法来修改Channel
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);
    void hasChannel(Channel* channel);

    // 判断EventLoop对象是否在自己的线程里面
    bool isInLoopThread() const  { return threadId_ == CurrentThread::tid(); }

private:

    
    // 执行回调  
    void doPendingFunctors();

    using ChannelList = std::vector<Channel*>;

    std::atomic_bool looping_;      //原子操作CAS
    std::atomic_bool quit_;         //标志退出loop循环
    
    const pid_t threadId_;          //记录当前loop线程的pid
    Timestamp pollReturnTime_;      //poller返回发生事件的channels的时间点
    std::shared_ptr<Poller> poller_;    //EventLoop所管理的Poller

    int wakeupFd_;      //用的是系统的eventfd，用于主loop与工作loop线程之间的通信，当mainLoop获取一个新用户的channel，通过轮询算法选择一个subloop，通过该成员唤醒subloop处理channel
    std::unique_ptr<Channel> wakeupChannel_;
    void handleRead();
    // eventloop类本身就维护了一个eventfd事件，这个事件是为了唤醒pool()的。
    // 此话怎讲？pool()毕竟是一个阻塞的函数，如果pool()所监听的事件在一段时间没有一个被激活，
    // 那么pool()就需要阻塞一段时间，如果此时我们不希望pool()阻塞在那里，要怎么办呢？
    // 我们就需要人工激活一个事件，打破pool的阻塞，这个事件就是eventfd事件（wakeupFd）。
    // 由于eventfd的跨线程的特性，我们就可在其他线程来打破evenloop对象所在线程的阻塞状态。


    ChannelList activeChannels_;
    Channel* currentActiveChannel_;

    std::atomic_bool callingPendingFunctors_;    //标识当前loop是否有需要执行回调的操作
    std::vector<Functor> pendingFunctors_;      //存储loop需要执行的所有回调操作
    std::mutex mutex_;      //用来保护上面vector容器的线程安全操作
};