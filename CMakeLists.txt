cmake_minimum_required(VERSION 3.31)
project(Kayles)

set(CMAKE_CXX_STANDARD 23)

add_executable(Kayles main.cpp
        kayles_client.cpp
        kayles_server.cpp)
