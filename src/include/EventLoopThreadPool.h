#pragma once
#include "noncopyable.h"
#include <functional>
#include <string>
#include <vector>
#include <memory>

class EventLoop;
class EventLoopThread;

class EventLoopThreadPool
{
public:
    using ThreadInitCallBack = std::function<void(EventLoop*)>;
    EventLoopThreadPool(EventLoop*baseLoop,const std::string&name);
    ~EventLoopThreadPool();

    void setThreadNum(int numThreads) { numThreads_ = numThreads; }

    void start(const ThreadInitCallBack& cb = ThreadInitCallBack());

    //如果工作在多线程中，baseLoop默认以轮询的方式分配channel给subloop
    EventLoop* getNextLoop();
    
    std::vector<EventLoop*> getAllLoops();

    bool started() const { return started_;}

    std::string name() const { return name_; }

private:
    EventLoop* baseLoop_;   // main()下的用户创建的EventLoop loop；最起码有一个loop
    std::string name_;
    bool started_;
    int numThreads_;
    int next_;
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::vector<EventLoop*> loops_;
};