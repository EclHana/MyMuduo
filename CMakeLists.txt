cmake_minimum_required(VERSION 3.0)

project(mymuduo)

#mymuduo最终编译成so动态库，设置动态库的路径放在根目录的lib文件夹下面
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

#设置头文件搜索路径
include_directories(${PROJECT_SOURCE_DIR}/src/include)

#设置调试信息
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fPIC")

add_subdirectory(src)
