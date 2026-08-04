cmake_minimum_required(VERSION 4.3)

project(SketchSync)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_VERBOSE_MAKEFILE ON)


add_executable(engine
        engine/engine.cpp
        common/websocket/websocket.h
        common/websocket/websocket.cpp
        common/websocket/result.h
)

add_executable(server
        server/server.cpp
        common/websocket/websocket.h
        common/websocket/websocket.cpp
        common/websocket/result.h
)


target_link_libraries(engine ws2_32)

target_link_libraries(server ws2_32)