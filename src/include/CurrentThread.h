#pragma once
#include <sys/syscall.h>
#include <unistd.h>

namespace CurrentThread
{
    extern __thread int t_cachedTid;//  __thread=thread_local,表示这个全局变量在当前线程是局部变量，外部线程无法得知这个变量的改变情况

    void cachedTid();

    inline int tid()
    {
        if(__builtin_expect(t_cachedTid==0, 0))//__builtin_expect是一个底层优化语句
        {
            cachedTid();
        }
        return t_cachedTid;
    }

}