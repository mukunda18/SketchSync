cmake_minimum_required(VERSION 4.3)

project(SketchSync)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_VERBOSE_MAKEFILE ON)

include_directories(${CMAKE_SOURCE_DIR})

add_executable(engine
        engine/engine.cpp
        engine/client/sessionClient.h
        engine/client/sessionClient.cpp
        common/webSocket/webSocket.h
        common/webSocket/webSocket.cpp
        common/tcp/tcpSocket.h
        common/tcp/tcpSocket.cpp
        common/protocol/message.h
        common/protocol/message.cpp
        common/results.h
        server/session/member.h
        server/session/session.h
        common/tcp/tcpServer.h
        common/tcp/tcpServer.cpp
        common/webSocket/webSocketServer.h
        common/webSocket/webSocketServer.cpp
        server/session/clientConnection.h
)

add_executable(server
        server/main.cpp
        common/webSocket/webSocket.h
        common/webSocket/webSocket.cpp
        common/tcp/tcpSocket.h
        common/tcp/tcpSocket.cpp
        common/protocol/message.h
        common/protocol/message.cpp
        common/results.h
        server/session/member.h
        server/session/session.h
        common/tcp/tcpServer.h
        common/tcp/tcpServer.cpp
        common/webSocket/webSocketServer.h
        common/webSocket/webSocketServer.cpp
        server/session/clientConnection.h
        server/server.cpp
)


target_link_libraries(engine ws2_32)

target_link_libraries(server ws2_32)