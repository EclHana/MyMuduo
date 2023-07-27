#include "CurrentThread.h"


namespace CurrentThread
{
    __thread int t_cachedTid = 0;//  __thread=thread_local,表示这个全局变量在当前线程是局部变量，外部线程无法得知这个变量的改变情况

    void cachedTid()
    {
        if(t_cachedTid==0)
        {
            t_cachedTid = static_cast<pid_t>(::syscall(SYS_gettid));//通过Linux系统调用获取当前线程tid值
        }
    }
}