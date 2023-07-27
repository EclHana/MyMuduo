#include "TcpServer.h"
#include "TcpConnection.h"
#include "Logger.h"
#include <strings.h>

EventLoop* CheckLoopNotNull(EventLoop*loop)
{
    if(!loop)
    {
        LOG_FATAL("%s:%s:%d mainLoop is null! \n",__FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

TcpServer::TcpServer(EventLoop*loop,const InetAddress& listenaddr,const std::string& nameArg,Option option)
    :loop_(CheckLoopNotNull(loop)), 
    ipPort_(listenaddr.toIpPort()),
    name_(nameArg),
    acceptor_(new Acceptor(loop, listenaddr, option==kNoReusePort)),//重要
    threadPool_(new EventLoopThreadPool(loop,name_)),//重要
    connectionCallback_(),
    messageCallback_(),
    nextConnId_(1),
    started_(0)
{
    //当有新用户，Acceptor::handleRead()会执行下面的回调函数
    using namespace std::placeholders;
    acceptor_->setNewConnectionCallback(std::bind(&TcpServer::newConnection,this, _1, _2));
}

TcpServer::~TcpServer()
{
    for(auto& item : connections_)
    {
        TcpConnectionPtr conn(item.second); //这个局部的sharedptr，出右括号，可以自动释放TcpConnection的对象资源
        item.second.reset(); //不能直接reset，否则无法执行下面这条函数
        conn->getLoop()->runInLoop(std::bind(&TcpConnection::connectDistory,conn));
    }
}

// 有一个新的客户端连接时，acceptor会执行这个回调
void TcpServer::newConnection(int sockfd, const InetAddress& peerAddr)
{
    // 轮询算法，选择一个subloop来管理channel
    EventLoop* ioLoop = threadPool_->getNextLoop();
    //组装连接的名字
    char buf[64] = {0};
    snprintf(buf,sizeof buf , "-%s#%d",ipPort_.c_str(),nextConnId_);
    nextConnId_++;//只有mainLoop处理
    std::string connName = name_ + buf;
    LOG_INFO("TcpServer::newConnection [%s] - new connection [%s] from %s \n",
                name_.c_str(),connName.c_str(),peerAddr.toIpPort().c_str());

    // 通过sockfd获取其绑定的本机的ip和port
    sockaddr_in loacl;
    ::bzero(&loacl,sizeof loacl);
    socklen_t addrlen = sizeof loacl;
    if(::getsockname(sockfd,(sockaddr*)&loacl,&addrlen) < 0)
    {
        LOG_ERROR("sockets::getLoaclAddr \n");
    }
    InetAddress loaclAddr(loacl);

    //根据成功连接的sockfd，创建TcpConnection对象
    TcpConnectionPtr conn(new TcpConnection(ioLoop,connName,sockfd,loaclAddr,peerAddr));
    connections_[connName] = conn;
    // 下面的回调都是用户设置给TcpServer>>TcpConnection>>Channel>>Poller>>notify Channel调用回调
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);

    // 设置了如何关闭连接的回调 
    // conn->shutdown >> shutdownInloop >> socketfd->shutdownWrite >> EpollHup >> Channel->closeCallback(TcpConnection给的)
    conn->setCloseCallback(std::bind(&TcpServer::removeConnection,this,std::placeholders::_1));

    // 直接调用TcpConnection::connectEstablished
    ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished,conn));
}

//设置底层subloop的个数
void TcpServer::setThreadNum(int numThreads)
{
    threadPool_->setThreadNum(numThreads);
}

void TcpServer::start()
{
    if(started_++==0)//防止一个TcpServer对象被start多次
    {
        threadPool_->start(threadInitCallback_);//启动底层Loop线程池，把subLoop全都开启，并loop.loop()
        loop_->runInLoop(std::bind(&Acceptor::listen,acceptor_.get()));
    }
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn)
{
    loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop,this,conn));
}
void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn)
{
    LOG_INFO("TcpServer::removeConnectionInLoop [%s] - connection%s \n",name_.c_str(),conn->name().c_str());
    connections_.erase(conn->name());
    EventLoop* ioLoop = conn->getLoop();
    ioLoop->queueInLoop(std::bind(&TcpConnection::connectDistory,conn));
}