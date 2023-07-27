#include "TcpConnection.h"
#include "Socket.h"
#include "Channel.h"
#include "EventLoop.h"
#include "Logger.h"
#include <functional>
#include <errno.h>

static EventLoop* CheckLoopNotNull(EventLoop*loop)
{
    if(!loop)
    {
        LOG_FATAL("%s:%s:%d TcpConnection Loop is null! \n",__FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}


TcpConnection::TcpConnection(EventLoop* loop,
                const std::string& name,
                int sockfd,
                const InetAddress& loaclAddr,
                const InetAddress& peerAddr)
    :loop_(CheckLoopNotNull(loop)),
    name_(name),
    state_(kConnecting),
    reading_(true),
    socket_(new Socket(sockfd)),
    channel_(new Channel(loop, sockfd)),
    loaclAddr_(loaclAddr),
    peerAddr_(peerAddr),
    highWaterMark_(64*1024*1024)    //64M
{
    // 下面给Channel设置相应的回调函数，Poller给Channel通知感兴趣的事件发生了，Channel会回调相应的函数
    channel_->setReadCallback(std::bind(&TcpConnection::handleRead,this,std::placeholders::_1));
    channel_->setWriteCallback(std::bind(&TcpConnection::handleWrite,this));
    channel_->setCloseCallback(std::bind(&TcpConnection::handleClose,this));
    channel_->setErrorCallback(std::bind(&TcpConnection::handleError,this));
    LOG_INFO("TcpConnection::ctor[%s] at fd=%d \n",name.c_str(),sockfd);
    socket_->setKeepAlive(true);

}
TcpConnection::~TcpConnection()
{
    LOG_INFO("TcpConnection::dtor[%s] at fd=%d state=%d \n",name_.c_str(), channel_->fd(), (int)state_);
}


void TcpConnection::handleRead(Timestamp receiveTime)
{
    int saveErrno = 0;
    ssize_t n = inputeBuffer_.readFd(channel_->fd(),&saveErrno);
    if(n>0)
    {
        // 已建立连接的用户，有可读事件发生了，调用用户传入的回调操作onMessage
        messageCallback_(shared_from_this(),&inputeBuffer_,receiveTime);
    }
    else if(n==0)
    {
        handleClose();
    }
    else
    {
        errno = saveErrno;
        LOG_ERROR("TcpConnection::handleRead \n");
        handleError();
    }
}

void TcpConnection::handleWrite()
{
    if(channel_->isWritting())
    {
        int saveErrno = 0;
        ssize_t n = outPutBuffer_.writeFd(channel_->fd(),&saveErrno);
        if(n>0)
        {
            outPutBuffer_.retrieve(n);
            if(outPutBuffer_.readableBytes()==0)//发送完成
            {
                channel_->disableWritting();
                if(writeCompleteCallback_)
                {
                    //唤醒loop_对应的线程，执行回调
                    loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
                }
                
                if(state_==kDisconnecting)
                {
                    // 某个地方调用了shutdown，但是数据没有发送完，所以是kDisconnecting，发送完数据后调用这个
                    shutdownInLoop();
                }
            }
        }
        else 
        {
            LOG_ERROR("TcpConnection handWrite \n");
        }
    }
    else 
    {
        LOG_ERROR("TcpConnection fd=%d is down, no more writting \n",channel_->fd());
    }
}

// poller >> channel::closeCallback >> TcpConnection::handleClose
void TcpConnection::handleClose()
{
    LOG_INFO("fd=%d state=%d \n",channel_->fd(),(int)state_);
    setState(kDisconnected);
    channel_->disableAll();

    TcpConnectionPtr connPtr(shared_from_this());
    connectionCallback_(connPtr);   //执行连接关闭的回调
    closeCallback_(connPtr);    //关闭连接的回调 ,执行的是TcpServer::removeConnection回调
}

void TcpConnection::handleError()
{
    int optval;
    socklen_t optlen = sizeof optval;
    int err = 0;
    if(::getsockopt(channel_->fd(),SOL_SOCKET,SO_ERROR,&optval,&optlen)<0)
    {
        err = errno;
    }
    else
    {
        err = optval;
    }
    LOG_ERROR("TcpConnection::handleError name:%s - SO_ERROR:%d \n",name_.c_str(),err);
}

// 发送数据
void TcpConnection::send(const std::string&buf)
{
    if(state_==kConnected)
    {
        if(loop_->isInLoopThread())
        {
            sendInLoop(buf.c_str(),buf.size());
        }
        else
        {
            loop_->runInLoop(std::bind(&TcpConnection::sendInLoop,this,buf.c_str(),buf.size()));
        }
    }
}

//发送数据，应用写的快，而内核发送数据慢，需要把待发送数据写入缓冲区，而且设置了水位回调
void TcpConnection::sendInLoop(const void* data, size_t len)
{
    ssize_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;

    //之前调用过connection的shutdown，不能再进行发送了
    if(state_ == kDisconnected)
    {
        LOG_ERROR("disconnected, give up writting");
        return;
    }

    //channel第一次开始写数据，而且缓冲区没有待发送数据
    if(!channel_->isWritting() && outPutBuffer_.readableBytes()==0)
    {
        nwrote = ::write(channel_->fd(), data, len);
        if(nwrote>=0)
        {
            remaining = len - nwrote;
            if(remaining==0 && writeCompleteCallback_)
            {
                //既然在这里数据一次性全部发送完成，就不用再给channel设置EPOLLOUT事件了
                loop_->queueInLoop(std::bind(writeCompleteCallback_,shared_from_this()));

            }
        }
        else    //nwrote < 0 
        {
            nwrote = 0;
            if(errno != EWOULDBLOCK)    //EWOULDBLOCK==EAGAIN
            {
                LOG_ERROR("TcpConnection::sendInLoop \n");
                if(errno==EPIPE || errno == ECONNRESET) //链接重置请求
                {
                    faultError = true;
                }
            }
        }
    }
    // 当前这次write并没有把数据全部发送出去，剩余数据需要保存到缓冲区当中，
    // 然后给channel注册EPOLLOUT事件，poller发现tcp的发送缓冲区有空间，
    // 会通知相应的sock（channel）调用writeCallback回调(实际上是Connection的handleWrite)
    // 最终也就是调用TcpConnection::handleWrite()，把缓冲区的数据全部发送完成
    if(!faultError && remaining>0)      
    {
        // 目前发送缓冲区剩余的待发送数据的长度
        size_t oldLen = outPutBuffer_.readableBytes();

        if( oldLen < highWaterMark_
            && oldLen + remaining >= highWaterMark_
            && highWaterMarkCallback_)
        {
            loop_->queueInLoop(std::bind(highWaterMarkCallback_,shared_from_this(),oldLen+remaining));
        }
        outPutBuffer_.append((char*)data + nwrote, remaining);
        if(!channel_->isWritting())
        {
            channel_->enableWritting();//这里一定要注册channel的写事件，否则poller不会给channel通知EPOLLOUT
        }
    }
}

// 连接建立
void TcpConnection::connectEstablished()
{
    setState(kConnected);
    channel_->tie(shared_from_this());// 将当前connection绑定到channel中，拿weakptr指向conn，防止conn被remove了channel还能执行conn给channel的回调函数
    channel_->enableReading();//向Poller中注册channel的epollin事件
    //新连接建立，执行回调
    connectionCallback_(shared_from_this());
}
// 连接销毁
void TcpConnection::connectDistory()
{
    if(state_==kConnected)
    {
        setState(kDisconnected);
        channel_->disableAll(); //把Channel的所有感兴趣的事件，从poller中del掉
        connectionCallback_(shared_from_this());
    }
    channel_->remove();     //把channel从poller中删除掉
}

//关闭连接
void TcpConnection::shutdown()
{
    if(state_==kConnected)
    {
        setState(kDisconnecting);
        loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop,this));
    }
}

void TcpConnection::shutdownInLoop()
{
    if(!channel_->isWritting())//说明outPutBuffer中数据已经全部发送完成
    {
        socket_->shutdownWrite();// 该函数内部会调用sockfd的shutdown，会触发EpollHup事件，然后调用channel的回调
    }
}
