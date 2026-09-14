cmake_minimum_required(VERSION 3.31)
project(Kayles)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(kayles_server
    kayles_server.cpp
    common.cpp
    err.cpp
    game_logic.cpp
    message_handlers.cpp
)

add_executable(kayles_client
    kayles_client.cpp
    common.cpp
    err.cpp
    game_logic.cpp
    client_messages.cpp
)