#pragma once
#include "noncopyable.h"
#include "Timestamp.h"
#include <functional>
#include <memory>

//理清楚EventLoop 、Channel、 Poller的关系，在Reactor模型上对应的Demultiplex
//channel理解为通道，封装了sockfd和其感兴趣的event，如EPOLLIN、EPOLLOUT事件
//还绑定了poller返回fd发生的具体事件

class EventLoop;


class Channel:noncopyable
{
public:
    using EventCallback = std::function<void()>;
    using ReadEventCallback = std::function<void(Timestamp)>;

    Channel(EventLoop* loop, int fd);
    ~Channel();

    //fd得到poller通知以后，处理事件的函数，调用相应的回调函数
    void handleEvent(Timestamp receiveTime);

    //设置对应事件的回调函数
    void setReadCallback(ReadEventCallback cb)
    { readCallback_ = std::move(cb); }  //必须是move，因为cb的生命周期只在这个函数中，出了作用域就会被析构掉

    void setWriteCallback(EventCallback cb)
    { writeCallback_ = std::move(cb); }

    void setCloseCallback(EventCallback cb)
    { closeCallback_ = std::move(cb); }

    void setErrorCallback(EventCallback cb)
    { errorCallback_ = std::move(cb); }

    //防止 channel被手动remove掉后channel还在执行回调函数
    void tie(const std::shared_ptr<void>&);

    int fd() const { return fd_; }
    int events() const { return events_; }
    int set_revents(int revt) { revents_ = revt; } //给poller用，poller监听到事件之后通过接口修改revent

    //设置fd相应的事件状态
    void enableReading() { events_ |= kReadEvent; update(); }
    void disableReading() { events_ &= ~kReadEvent; update(); }
    void enableWritting() { events_ |= kWriteEvent; update(); }
    void disableWritting() { events_ &= ~kWriteEvent; update(); }
    void disableAll() {events_ = kNoneEvent; update(); }

    //返回fd当前事件状态
    bool isNoneEvent() const { return events_ == kNoneEvent; }
    bool isWritting() const { return events_ == kWriteEvent; }
    bool isReading() const { return events_ == kReadEvent; }

    int index() { return index_; }
    void set_index(int idx) { index_ = idx; }

    //当前的channel 属于哪个EventLoop
    EventLoop* ownerLoop() { return loop_; }
    void remove();

private:

    void update();
    void handleEventWithGuard(Timestamp receiveTime);

    static const int kNoneEvent;
    static const int kReadEvent;
    static const int kWriteEvent;

    EventLoop* loop_;           // 事件循环   
    const int fd_;          //poller监听的对象
    int events_;            //注册fd感兴趣的事件
    int revents_;           //poller返回fd具体发送的事件real event
    int index_;    

    std::weak_ptr<void> tie_;        //防止对象过早析构，void可以接受任意类型的指针
    bool tied_; 

    //因为channel通道里面能够获知fd最终发生的具体事件的revents，所以它负责调用具体事件的回调操作
    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;

};