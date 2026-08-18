#ifndef SKETCHSYNC_ENGINE_H
#define SKETCHSYNC_ENGINE_H

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef NOGDI
#define NOGDI
#endif

#include "raylib.h"
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#include "common/tcp/tcpSocket.h"
#include "common/websocket/websocket.h"
#undef ShowCursor
#undef CloseWindow
#include "engine/canvas/canvas.h"
#include "engine/client/sessionClient.h"
#include "engine/client/network_session_state.h"
#include "engine/persistence/persistenceWriter.h"
#include "engine/process/server_process.h"

// MinGW maps the Win32 text API through a function-like DrawText macro.
// Keep that macro from rewriting raylib's DrawText declaration/calls.
#ifdef DrawText
#undef DrawText
#endif

#include "engine/ui/app_layout.h"

struct sketch_app
{
    sketch_app();
    ~sketch_app();

    [[nodiscard]] int run();

private:
    static std::filesystem::path resolve_server_executable();
    void set_status(std::string value);
    [[nodiscard]] std::string get_status() const;
    void open_and_load();
    void join_session();
    void create_session();
    void leave_session(); 
    void handle_leave();
    void clear_canvas();
    void process_canvas_input(const canvas_input_state& input);

    void poll_session();

    result<bool> start_local_server();
    result<bool> connect_to_server(std::chrono::milliseconds timeout = std::chrono::milliseconds(3000));
    void async_connect_to_server();
    void async_start_local_server();
    void stop_connect_thread();
    void handle_disconnect();
    void disconnect();
    void stop_local_server();
    void rebuild_render_texture();

    void handle_notification(const std::vector<uint8_t>& payload);
    void handle_draw(const std::vector<uint8_t>& payload);
    void handle_canvas_state(const std::vector<uint8_t>& payload);
    void handle_ack(const Message& msg);
    void handle_error(const std::vector<uint8_t>& payload);

    canvas surface;
    Texture2D canvas_texture{};
    std::vector<Color> upload_buffer;
    std::string current_file = "untitled";
    std::string status = "Ready";
    std::atomic<bool> dirty{true};
    std::optional<draw_operation> active_stroke;
    tool_type active_tool = tool_type::freehand;
    uint32_t active_color = 0xFF1F1F1F;
    uint8_t active_thickness = 2;

    network_session_state net_state;
    ui::AppLayout layout;

    std::atomic<uint32_t> next_operation_number{1};
    std::unordered_set<uint64_t> pending_operations;
    mutable std::mutex pending_mutex;

    server_process local_server;
    std::unique_ptr<persistence_writer> operation_log;
    std::unique_ptr<net::io_context> io_context;
    std::unique_ptr<tcpSocket> tcp_socket;
    std::unique_ptr<webSocket> ws_socket;
    std::unique_ptr<sessionClient> session_client;

    std::thread poll_thread;
    std::thread connect_thread;
    std::atomic<bool> stop_poll{false};
    std::atomic<bool> connecting{false};
    std::atomic<net::io_context*> connecting_io{nullptr};

    mutable std::mutex status_mutex;
    mutable std::mutex net_mutex;
};

#endif
