#pragma once
#include "Poller.h"
#include "Timestamp.h"
#include <vector>
#include <sys/epoll.h>


// epoll的使用 epoll_create  epoll_ctl(add/mod/del)   epoll_wait

class EPollPoller : public Poller
{
public:
    EPollPoller(EventLoop* loop);   //epoll_create
    ~EPollPoller() override;        //close epollfd    

    //重写基类Poller的方法
    Timestamp poll(int timeoutMs, ChannelList* activeChannels) override;   //epoll_wait

    //epoll_ctl(add/mod/del)
    void updateChannel(Channel* channel) override;
    void removeChannel(Channel* channel) override;
    
private:
    static const int kInitEventListSize = 16;

    //填写活跃的通道
    void fillActiveChannels(int numEvents, ChannelList* activeChannels) const;
    //更新Channel通道
    void update(int operation, Channel* channel);

    using EventList = std::vector<epoll_event>;

    int epollfd_;
    EventList events_;
};