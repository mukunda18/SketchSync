cmake_minimum_required(VERSION 4.3)

project(SketchSync)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_VERBOSE_MAKEFILE ON)


add_executable(engine
        engine/engine.cpp
        common/websocket/websocket.h
        common/websocket/websocket.cpp
        common/tcp/tcp_socket.h
        common/tcp/tcp_socket.cpp
        common/protocol/message.h
        common/protocol/message.cpp
        common/results.h
        common/session/member.h
        common/session/session.h
        common/tcp/tcp_server.h
        common/tcp/tcp_server.cpp
        common/websocket/websocket_server.h
        common/websocket/websocket_server.cpp
)

add_executable(server
        engine/engine.cpp
        common/websocket/websocket.h
        common/websocket/websocket.cpp
        common/tcp/tcp_socket.h
        common/tcp/tcp_socket.cpp
        common/protocol/message.h
        common/protocol/message.cpp
        common/results.h
        common/session/member.h
        common/session/session.h
)


target_link_libraries(engine ws2_32)

target_link_libraries(server ws2_32)