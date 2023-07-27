#pragma once

/*拷贝构造和赋值操作delete，派生类构造需要调用基类的构造函数，如果被拷贝会出错*/

class noncopyable
{
public:
    noncopyable(const noncopyable&)=delete;
    noncopyable& operator=(const noncopyable&)=delete;
protected:
    noncopyable()=default;
    ~noncopyable()=default;
};