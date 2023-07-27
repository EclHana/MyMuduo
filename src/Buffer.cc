#include "Buffer.h"
#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>

// 从fd上读取数据, Poller工作在LT模式（会一直上报）
// buffer缓冲区是有大小的，但是从fd上读数据的时候，却不知道tcp缓存区的数据大小
ssize_t Buffer::readFd(int fd, int* saveErrno)
{
    char extrabuf[65536] = {0};//栈上内存空间 64k
    
    struct iovec vec[2];

//   struct iovec
//   {
//     void *iov_base;	/* Pointer to data.  */
//     size_t iov_len;	/* Length of data.  */
//   };

    const size_t writable = writableBytes();//底层buffer缓冲区剩余的可写大小
    vec[0].iov_base = begin() + writerIndex_;
    vec[0].iov_len = writable;

    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof extrabuf;
    
    const int iovcnt = (writable<sizeof extrabuf) ? 2 : 1;//一次最多读64k的数据
    //::readv()可以在非连续的多个缓冲区中写入同一个fd的数据，第二个参数是struct iovec vec数组的地址，第三个参数是可写数组的大小
    const ssize_t n = ::readv(fd,vec,iovcnt);

    if(n<0)
    {
        *saveErrno = errno;
    }
    else if(n<=writable)//buffer缓冲区已经够存储读出来的数据了
    {
        writerIndex_+=n;
    }
    else //extrabuf也写入数据，说明可写空间不足，需要扩容
    {
        writerIndex_ = buffer_.size();//原来的buffer缓冲区已经写满了
        append(extrabuf, n-writable);//开始把extrabuf里面的数据放到buffer中（会自动扩容）
    }
    return n;
}


// 通过fd发送数据
ssize_t Buffer::writeFd(int fd, int* saveErrno)
{
    ssize_t n = ::write(fd,peek(),readableBytes());
    if(n<0)
    {
        *saveErrno = errno;
    }
    return n;
}
    