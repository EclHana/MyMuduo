#pragma once

#include <vector>
#include <string>
#include <algorithm>


/// A buffer class modeled after org.jboss.netty.buffer.ChannelBuffer
///
/// 
/// +-------------------+------------------+------------------+
/// | prependable bytes |  readable bytes  |  writable bytes  |
/// |                   |     (CONTENT)    |                  |
/// +-------------------+------------------+------------------+
/// |                   |                  |                  |
/// 0      <=      readerIndex   <=   writerIndex    <=     size
/// 


// 网络库底层缓冲区类
class Buffer
{
public:
    static const size_t kCheapPrepend = 8;  //报文头长度
    static const size_t kInitialSize = 1024;

    explicit Buffer(size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend+initialSize),
        readerIndex_(kCheapPrepend),
        writerIndex_(kCheapPrepend)
    {}

    size_t readableBytes() const
    {
        return writerIndex_-readerIndex_;
    }

    size_t writableBytes() const
    {
        return buffer_.size() - writerIndex_;
    }

    size_t prependableBytes() const
    {
        return readerIndex_;
    }

    //返回缓冲区中的可读数据的起始地址
    const char* peek() const
    {
        return begin() + readerIndex_;
    }

    // onMessage string <- Buffer
    void retrieve(size_t len)
    {
        if(len<readableBytes())
        {
            readerIndex_+=len;// 应用只读取了可读缓冲区数据的一部分，就是len长度，还剩下readerIndex_到writerIndex_数据没读
        }
        else//len == readableBytes()
        {
            retrieveAll();
        }
    }

    void retrieveAll()
    {
        readerIndex_ = writerIndex_ = kCheapPrepend;//复位
    }

    //把onMessage函数上报的buffer数据转成string类型的数据
    std::string retrieveAllAsString()
    {
        return retrieveAsString(readableBytes());//应用可读取数据的长度
    }
    
    std::string retrieveAsString(size_t len)
    {
        std::string res(peek(),len);
        retrieve(len);//上一行把缓冲区中可读的数据，已经读取出来，这里肯定要对缓冲区进行复位操作
        return res;
    }

    //  确保空间能放下len长度的数据（不够就扩容）
    void ensureWritableBytes(size_t len)
    {
        if(writableBytes()<len)
        {
            makeSpace(len);//扩容函数
        }
    }

    // 把[data, data+len]的数据放到writeable缓冲区中
    void append(const char* data, size_t len)
    {
        ensureWritableBytes(len);
        std::copy(data,data+len,beginWrite());
        writerIndex_+=len;
    }
    
    char* beginWrite()
    {
        return begin()+writerIndex_;
    }

    const char* beginWrite() const 
    {
        return begin()+writerIndex_;
    }

    // 从fd上读取数据
    ssize_t readFd(int fd, int* saveErrno);

    // 通过fd发送数据
    ssize_t writeFd(int fd, int* saveErrno);
    
private:

    void makeSpace(size_t len)
    {
        // 读完之后readindex后移，这样readindex到kCheapPrepend的空间就空出来了，
        // 如果空出来的空间加上可写的空间足够放下len长度的数据那就不用扩容，把数据挪一下就行了
        // 但是如果这样也不够的话，那就不挪了，直接扩容
        if(writableBytes() + prependableBytes() < len + kCheapPrepend)
        {
            buffer_.resize(writerIndex_+len);
        }
        else
        {
            size_t readable = readableBytes();
            std::copy(begin()+readerIndex_, begin()+writerIndex_, begin()+kCheapPrepend);
            readerIndex_ = kCheapPrepend;
            writerIndex_ = kCheapPrepend + readable;
        }
    }

    char* begin()
    {
        return &(*buffer_.begin());
    }
    const char* begin() const
    {
        return &(*buffer_.begin());
    }

    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;
};