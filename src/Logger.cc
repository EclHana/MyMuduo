#include "Logger.h"
#include "Timestamp.h"
#include <iostream>

//获取日志唯一实例
    Logger& Logger::instance()
    {
        static Logger logger;
        return logger;
    }
    //设置日志级别
    void Logger::setLogLevel(int level)
    {
        logLevel_ = level;
    }
    //写日志 [级别信息] time ：msg
    void Logger::log(std::string msg)
    {
        switch(logLevel_)
        {
            case INFO:
                std::cout<<"[INFO]";
                break;
            case ERROR:
                std::cout<<"\033[33m[ERROR]\033[0m";
                break;
            case FATAL:
                std::cout<<"\033[31m[FATAL]\033[0m";
                break;
            case DEBUG:
                std::cout<<"\033[34m[DEBUG]\033[0m";
                break;
            default:
                break;
        }
        
        //打印时间
        std::cout<<Timestamp::now().toString()<<" : "<<msg <<std::endl;
    }