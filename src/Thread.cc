#include "Thread.h"
#include "CurrentThread.h"
#include <semaphore.h>

std::atomic_int Thread::numCreated_{0};


Thread::Thread(ThreadFunc func, const std::string &name)
    :started_(false),joined_(false),tid_(0),func_(std::move(func)),name_(name)
{
    setDefaultName();
}

Thread::~Thread()
{
    // 创建一个可运行(创建时传入线程函数)的thread线程对象后，
    //必须对该线程对象进行处理，要么调用join()，要么调用detach()，否则线程对象析构时程序将直接退出。

    if(started_ && !joined_)//Thread生命周期到期后，把新线程detach，防止新线程被terminate()
    {
        thread_->detach();  //thread类提供的设置分离线程的方法
    }
}

void Thread::start()//一个Thread对象，记录的就是一个新线程的详细信息
{
    started_ = true;

    sem_t sem;
    sem_init(&sem,false,0);

    thread_ = std::make_shared<std::thread>([&](){
        //获取线程tid
        tid_ = CurrentThread::tid();
        sem_post(&sem);

        // 开启一个新线程，专门执行该线程函数
        func_();
    });

    // 这里必须先等待获取上面新创建的线程 tid 值
    sem_wait(&sem);//保证确实创建了新线程再离开作用域
}

void Thread::join()
{
    joined_ = true;
    thread_->join();
}

void Thread::setDefaultName()
{
    int num = ++numCreated_;
    if(name_.empty())
    {
        char buf[32] = {0};
        snprintf(buf,sizeof buf,"Thread%d",num);
        name_ = buf;
    }
}