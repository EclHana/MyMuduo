#pragma once
#include "noncopyable.h"
#include "InetAddress.h"
#include "Callbacks.h"
#include "Timestamp.h"
#include "Buffer.h"

#include <memory>
#include <string>
#include <atomic>


class Channel;
class EventLoop;
class Socket;
class Buffer;

/**
 * TcpServer 通过acceptor 有一个新用户连接，通过accept函数拿到connfd
 * 打包TcpConnection，设置相应的回调，相应的Channel也设置相应回调，然后放到Poller上
 * Poller监听到事件发生之后，触发Channel的回调函数
 * 
*/


class TcpConnection : noncopyable, public std::enable_shared_from_this<TcpConnection>
{
public:
    TcpConnection(EventLoop* loop,
                const std::string& name,
                int sockfd,
                const InetAddress& loaclAddr,
                const InetAddress& peerAddr);
    ~TcpConnection();

    EventLoop* getLoop() const { return loop_; }

    const std::string& name() const { return name_; }
    const InetAddress& loaclAddress() const { return loaclAddr_; }
    const InetAddress& peerAddress() const { return peerAddr_; }

    bool connected() const { return state_ == kConnected; }
    // 发送数据
    void send(const std::string&buf);

    //关闭连接
    void shutdown(); 

    void setConnectionCallback(const ConnectionCallback& cb)
    { connectionCallback_ = cb; }

    void setCloseCallback(const ConnectionCallback& cb)
    { closeCallback_ = cb; }

    void setMessageCallback(const MessageCallback& cb)
    { messageCallback_ = cb; }

    void setWriteCompleteCallback(const WriteCompleteCallback& cb)
    { writeCompleteCallback_ = cb; }

    void setHighWaterMarkCallback(const HighWaterMarkCallback& cb, size_t highWaterMark)
    { highWaterMarkCallback_ = cb; highWaterMark_ = highWaterMark; }

    // 连接建立
    void connectEstablished();
    // 连接销毁
    void connectDistory();

    // 这里的handle的意思是给channel提交回调函数，也就是说，channel的回调函数都是源自这里的
    void handleRead(Timestamp receiveTime);
    void handleWrite();
    void handleClose();
    void handleError();

    void sendInLoop(const void* message, size_t len);
    
    
    
private:

    void shutdownInLoop();

    enum State{ kDisconnected, kConnecting, kConnected, kDisconnecting };
    void setState(State state) { state_ = state; };

    EventLoop* loop_;   // 多线程情况下，这里绝对不是mainLoop，因为tcpConnection都是在subLoop管理的
    const std::string name_;
    std::atomic_int state_;
    bool reading_;
    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_;

    const InetAddress loaclAddr_;
    const InetAddress peerAddr_;

    ConnectionCallback connectionCallback_;//有新连接时的回调
    MessageCallback messageCallback_;//有读写消息时的回调
    WriteCompleteCallback writeCompleteCallback_;//消息发送完成之后的回调
    HighWaterMarkCallback highWaterMarkCallback_;
    CloseCallback closeCallback_;

    size_t highWaterMark_;

    Buffer inputeBuffer_;   //接受数据的缓冲区
    Buffer outPutBuffer_;   //发送数据的缓冲区
};