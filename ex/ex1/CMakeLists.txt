cmake_minimum_required(VERSION 3.28.3)
project(ex1)

set(CMAKE_CXX_STANDARD 11)

include_directories(.)

add_executable(ex1
    measure.cpp
    measure.h
    memory_latency.cpp
    memory_latency.h)
