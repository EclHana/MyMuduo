#include "EventLoopThreadPool.h"
#include "EventLoopThread.h"
#include <memory>

EventLoopThreadPool::EventLoopThreadPool(EventLoop*baseLoop,const std::string&name)
    :baseLoop_(baseLoop),
    name_(name),
    started_(false),
    numThreads_(0),
    next_(0)
{

}

EventLoopThreadPool::~EventLoopThreadPool()
{
    //没有必要去delete loop，因为loop是线程栈上创建的 
}


void EventLoopThreadPool::start(const ThreadInitCallBack& cb)
{
    started_ = true;
    for(int i = 0;i<numThreads_;i++)
    {
        char buf[name_.size() + 32] = {0};
        snprintf(buf,sizeof buf,"%s%d",name_.c_str(),i);
        EventLoopThread *t = new EventLoopThread(cb,buf);
        threads_.emplace_back(std::unique_ptr<EventLoopThread>(t));
        loops_.emplace_back(t->startLoop());
    }
    //整个服务端只有一个线程，也就是baseLoop
    if(numThreads_==0 && cb)
    {   
        cb(baseLoop_);
    }
}

//如果工作在多线程中，baseLoop默认以 轮询 的方式分配channel给subloop，获取下一个处理事件的loop
EventLoop* EventLoopThreadPool::getNextLoop()
{
    EventLoop *loop = baseLoop_;

    if(!loops_.empty())
    {
        loop = loops_[next_];   
        ++next_;                //轮询
        if(next_>=loops_.size())
            next_ = 0;
    }

    return loop;
}

std::vector<EventLoop*> EventLoopThreadPool::getAllLoops()
{
    if(loops_.empty())
    {
        return {baseLoop_};
    }
    else
    {
        return loops_;
    }
}   