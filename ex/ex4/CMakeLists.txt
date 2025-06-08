cmake_minimum_required(VERSION 3.22)
project(ex4)

set(CMAKE_CXX_STANDARD 20)

add_executable(ex4
        Resources/VirtualMemory.cpp
        Resources/PhysicalMemory.cpp
        Resources/SimpleTest.cpp)
