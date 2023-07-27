#include "Acceptor.h"
#include <sys/socket.h>
#include <sys/types.h>
#include "Logger.h"
#include <unistd.h>
#include "InetAddress.h"

static int createNonblocking()
{
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if(sockfd<0)
    {
        LOG_FATAL("%s:%s:%d  listen socket create error:%d \n", __FILE__, __FUNCTION__, __LINE__, errno);
    }
}

Acceptor::Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport)
    :loop_(loop),
    acceptSocket_(createNonblocking()),//创建非阻塞的socket
    acceptChannel_(loop,acceptSocket_.fd()),//打包acceptChannel
    listenning_(false)
{
    acceptSocket_.setReuseAddr(true);
    acceptSocket_.setReusePort(true);
    acceptSocket_.bindAddress(listenAddr);// 绑定socket
    // TcpServer::start()  Acceptor.listen 有新用户连接，需要执行一个回调来（connfd >> Channel >> subLoop） 
    // baseLoop 监听到 acceptChannel有事件发生时会调用handleRead
    acceptChannel_.setReadCallback(std::bind(&Acceptor::handleRead, this));//只关心新用户连接的读事件
}

Acceptor::~Acceptor()
{
    acceptChannel_.disableAll();
    acceptChannel_.remove();
}

void Acceptor::listen()
{
    listenning_ = true;
    acceptSocket_.listen();
    acceptChannel_.enableReading(); //acceptChannel >> Poller 
}

// listenfd有事件发生，有新用户连接
void Acceptor::handleRead()
{
    InetAddress peetAddr;
    int connfd = acceptSocket_.accept(&peetAddr);
    if(connfd>=0)
    {
        if(newConnectionCallback_)
        {
            newConnectionCallback_(connfd,peetAddr);//回调中轮询找到subLoop，唤醒它，分发当前新连接的用户Channel
        }
        else
        {
            ::close(connfd);
        }
    }
    else
    {
        LOG_ERROR("%s:%s:%d  accept error:%d \n", __FILE__, __FUNCTION__, __LINE__, errno);
        if(errno==EMFILE)
        {
            LOG_ERROR("%s:%s:%d  sockfd reached limit \n", __FILE__, __FUNCTION__, __LINE__);
        }
    }
}