#include "EPollPoller.h"
#include "Logger.h"
#include "Channel.h"
#include <unistd.h>
#include <errno.h>
#include <string.h>

//对应的是Channel 的 index_
const int kNew = -1;        //一个Channel还未添加到Poller里面
const int kAdded = 1;       //一个Channel已经添加到Poller里面   
const int kDeleted = 2;     //一个Channel已经删除了从Poller里面

//epoll_create
EPollPoller::EPollPoller(EventLoop* loop)
    :Poller(loop),
    epollfd_(::epoll_create1(EPOLL_CLOEXEC)),//EPOLL_ CLOEXEC这个标志位可以确保在 fork 子进程时，子进程不会继承 epoll 实例的文件描述符。
    events_(kInitEventListSize)//events_是一个存事件的vector
{
    if(epollfd_<0)
    {
        LOG_FATAL("epoll_create error:%d \n",errno);
    }
}   

//close epollfd    
EPollPoller::~EPollPoller()
{
    ::close(epollfd_);
}


//重写基类Poller的方法 epoll_wait
Timestamp EPollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
    //实际上应该用LOG_DEBUG
    LOG_INFO("func=%s >> fd total count:%d \n",__FUNCTION__, channels_.size());
    int numEvents = ::epoll_wait(epollfd_, &(*events_.begin()), static_cast<int>(events_.size()),timeoutMs);// &*(events_.begin()) events_是vector，begin（）返回首元素的迭代器，对迭代器解引用*得到首元素，然后对首元素取地址&得到数组的首元素地址
    int saveErrno = errno;
    Timestamp now(Timestamp::now());
    if(numEvents > 0)
    {
        LOG_INFO("%d events happened \n",numEvents);
        fillActiveChannels(numEvents,activeChannels);
        if(numEvents==events_.size())//如果当前触发的事件数量已经等于EventList的大小，说明需要扩容了
        {
            events_.resize(events_.size()*2);
        }
    }
    else if(numEvents==0)
    {
        LOG_DEBUG("%s timeout! \n", __FUNCTION__);
    }
    else
    {
        if(saveErrno!=EINTR)
        {
            errno = saveErrno;//保证拿到当前Loop的errno
            LOG_ERROR("EPollPoller::poll() error");
        }
    }
    return now;
}

//Channel:: update remove => EventLoop:: updateChannel removeChannel => EPollPoller :: updateChannel removeChannel

void EPollPoller::updateChannel(Channel* channel)
{
    const int index = channel->index();
    LOG_INFO("func=%s, fd = %d, events = %d, index = %d \n",__FUNCTION__, channel->fd(),channel->events(),index);

    if(index==kNew||index==kDeleted)
    {
        if(index==kNew)
        {
            int fd = channel->fd();
            channels_[fd] = channel;
        }
        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD,channel);
    }
    else    //channel已经在poller上注册过了
    {
        int fd = channel->fd();
        if(channel->isNoneEvent())
        {
            update(EPOLL_CTL_DEL,channel);
            channel->set_index(kDeleted);
        }
        else
        {
            update(EPOLL_CTL_MOD,channel);
        }
    }
}

//从poller中删除channel
void EPollPoller::removeChannel(Channel* channel)
{
    
    int fd = channel->fd();
    channels_.erase(fd);
    LOG_INFO("func=%s, fd = %d,\n",__FUNCTION__, fd);
    int index = channel->index();
    if(index==kAdded)
    {
        update(EPOLL_CTL_DEL,channel);
    }
    channel->set_index(kNew);
}


//填写活跃的通道
void EPollPoller::fillActiveChannels(int numEvents, ChannelList* activeChannels) const
{
    for(int i = 0;i<numEvents;i++)
    {
        Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->set_revents(events_[i].events);
        activeChannels->push_back(channel);//EventLoop就拿到了它的poller给他返回的所有发生事件的channel列表
    }
}

//更新Channel通道,epoll_ctl->add/mod/del
void EPollPoller::update(int operation, Channel* channel)
{
    epoll_event event;
    memset(&event, 0, sizeof event);
    int fd = channel->fd();
    event.events = channel->events();
    event.data.fd = fd;
    event.data.ptr = channel;
    

    if(::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    {
        if(operation==EPOLL_CTL_DEL)
        {
            LOG_ERROR("epoll_ctl_del error:%d \n" ,errno);
        }
        else
        {
            LOG_FATAL("epoll_ctl add/mod error:%d \n", errno);
        }
    }
}
