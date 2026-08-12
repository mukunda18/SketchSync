cmake_minimum_required(VERSION 4.3)

project(SketchSync)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(CMAKE_VERBOSE_MAKEFILE ON)

include_directories(${CMAKE_SOURCE_DIR})

# Local raylib installation
set(RAYLIB_ROOT "${CMAKE_SOURCE_DIR}/external/raylib")

add_library(raylib STATIC IMPORTED GLOBAL)

set_target_properties(raylib PROPERTIES
    IMPORTED_LOCATION "${RAYLIB_ROOT}/lib/libraylib.a"
    INTERFACE_INCLUDE_DIRECTORIES "${RAYLIB_ROOT}/include"
)

add_executable(engine
    engine/main.cpp
    engine/engine.cpp
    engine/engine.h
    engine/process/server_process.cpp
    engine/process/server_process.h
    engine/client/sessionClient.cpp
    engine/client/sessionClient.h
    engine/persistence/persistenceWriter.cpp
    engine/persistence/persistenceWriter.h
    engine/ui/ui.cpp
    engine/ui/ui.h
    engine/ui/file_dialog.cpp
    engine/ui/file_dialog.h
    engine/canvas/canvas.cpp
    engine/canvas/canvas.h
    common/canvas/draw_operation.h
    common/canvas/draw_operation.cpp
    common/protocol/message.h
    common/protocol/message.cpp
    common/tcp/tcpSocket.cpp
    common/tcp/tcpSocket.h
    common/websocket/websocket.cpp
    common/websocket/websocket.h
    common/results.h
)

add_executable(server
    server/main.cpp
    common/canvas/draw_operation.h
    common/canvas/draw_operation.cpp
    common/webSocket/webSocket.h
    common/webSocket/webSocket.cpp
    common/tcp/tcpSocket.h
    common/tcp/tcpSocket.cpp
    common/protocol/message.h
    common/protocol/message.cpp
    common/results.h
    server/session/member.h
    server/session/session.h
    engine/canvas/canvas.h
    server/session/clientConnection.h
    server/server.cpp
    engine/canvas/canvas.cpp
    common/tcp/tcpServer.h
    common/tcp/tcpServer.cpp
    common/webSocket/webSocketServer.h
    common/webSocket/webSocketServer.cpp
)

target_link_libraries(engine
    raylib
    ws2_32
    comdlg32
    gdi32
    winmm
    opengl32
    shell32
    user32
)

target_compile_definitions(engine PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    NOGDI
)

target_link_libraries(server
    ws2_32
)

target_compile_definitions(server PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    NOGDI
)
