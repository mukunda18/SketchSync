#ifndef SKETCHSYNC_NETWORK_MANAGER_H
#define SKETCHSYNC_NETWORK_MANAGER_H

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef NOGDI
#define NOGDI
#endif

#include "common/tcp/tcpSocket.h"
#include "common/websocket/websocket.h"
#include "common/results.h"
#include "engine/network/connection_types.h"
#include "engine/process/server_process.h"
#include "common/network_constants.h"

struct session_manager;

struct network_manager
{
    explicit network_manager(std::function<void(std::string)> set_status);
    ~network_manager();

    network_manager(const network_manager&) = delete;
    network_manager& operator=(const network_manager&) = delete;

    void set_session(session_manager& session);

    result<bool> connect_to_server(std::chrono::milliseconds timeout = std::chrono::milliseconds(3000));
    void async_connect_to_server();
    void async_tcp_discover_and_join(uint32_t session_id);
    void async_ws_connect_and_join(uint32_t session_id);
    void async_ws_connect_and_create();
    void async_start_local_server();
    result<bool> start_local_server();
    void disconnect();
    void stop_local_server();
    void shutdown();
    void stop_connect_thread();
    void request_stop();
    [[nodiscard]] bool stop_requested() const;

    [[nodiscard]] bool toggle_protocol();
    std::string& host();
    std::string& port();
    [[nodiscard]] std::string host_copy() const;
    [[nodiscard]] std::string port_copy() const;
    [[nodiscard]] connection_protocol protocol() const;
    [[nodiscard]] bool connected() const;
    [[nodiscard]] connection_state state() const;
    [[nodiscard]] bool is_connecting() const;
    [[nodiscard]] bool local_server_running() const;

    void set_disconnected();
    void set_endpoint(const std::string& host, const std::string& port, connection_protocol protocol);

private:
    static std::filesystem::path resolve_server_executable();
    void finish_connect_attempt(bool reset_session_if_failed);
    void finish_connect_io();
    void close_sockets() const;
    void handle_disconnect();

    std::function<void(std::string)> set_status_;
    session_manager* session_ = nullptr;

    connection_protocol protocol_ = connection_protocol::tcp;
    std::string host_ = std::string(net_config::DEFAULT_HOST);
    std::string port_ = std::string(net_config::DEFAULT_TCP_PORT_STR);
    bool connected_ = false;
    connection_state state_ = connection_state::disconnected;

    server_process local_server_;
    std::unique_ptr<net::io_context> io_context_;
    std::unique_ptr<tcpSocket> tcp_socket_;
    std::unique_ptr<webSocket> ws_socket_;

    std::thread connect_thread_;
    std::atomic<bool> stop_poll_{false};
    std::atomic<bool> connecting_{false};
    std::atomic<net::io_context*> connecting_io_{nullptr};
    std::atomic<bool> shut_down_{false};

    mutable std::mutex mutex_;
};

#endif
