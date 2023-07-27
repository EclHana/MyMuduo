#include "EventLoopThread.h"
#include "EventLoop.h"


EventLoopThread::EventLoopThread(const ThreadInitCallBack&cb,const std::string&name)
        :loop_(nullptr),
        exiting_(false),
        thread_(std::bind(&EventLoopThread::threadFunc,this),name),
        mutex_(),
        cond_(),
        callback_(cb)
{
    
}

EventLoopThread::~EventLoopThread()
{
    exiting_ = true;
    if(loop_!=nullptr)
    {
        loop_->quit();
        thread_.join();//等待子线程结束
    }
}

EventLoop* EventLoopThread::startLoop()
{
    thread_.start();//启动底层新线程,调用构造中绑定好的EventLoopThread::threadFunc()，等待loop_不是空指针

    EventLoop *loop = nullptr;

    {
        std::unique_lock<std::mutex> lock(mutex_);
        while(loop_==nullptr)
        {
            cond_.wait(lock);//等待loop_被threadFunc()构造成功
        }
        loop = loop_;
    }
    return loop;
}

// 下面这个方法，是在单独新线程里面运行的线程入口函数
void EventLoopThread::threadFunc()
{
    // 创建一个独立的EventLoop，和上面的线程是一一对应的，one loop per thread
    EventLoop loop;

    if(callback_)
    {
        callback_(&loop);
    }
    
    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop;
        cond_.notify_one();
    }

    loop.loop();
    
    //最后服务器关闭的时候会将loop_置空
    std::unique_lock<std::mutex> lock(mutex_);
    loop_ = nullptr;
}