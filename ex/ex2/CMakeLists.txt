cmake_minimum_required(VERSION 3.28)
project(ex2)

set(CMAKE_CXX_STANDARD 20)

add_executable(ex2 test0_sanity.cpp
        uthreads.cpp
        thread_manager.h
        thread_manager.cpp
        thread.h
        thread.cpp
        alarm.h
        alarm.cpp
)
