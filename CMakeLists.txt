cmake_minimum_required(VERSION 3.31)
project(Kayles)

set(CMAKE_CXX_STANDARD 23)

add_executable(Kayles
        kayles_client.cpp
        kayles_server.cpp
        common.cpp
        common.h
        game_logic.h
        err.h
        err.cpp
        game_logic.cpp
)
