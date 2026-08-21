#include "engine/network/network_manager.h"

#include <exception>
#include <utility>

#include "common/udp/udpDiscovery.h"
#include "engine/session/session_manager.h"
#include "engine/session/sessionClient.h"

network_manager::network_manager(std::function<void(std::string)> set_status)
    : set_status_(std::move(set_status))
{
}

network_manager::~network_manager()
{
    shutdown();
}

void network_manager::set_session(session_manager& session)
{
    session_ = &session;
}

void network_manager::request_stop()
{
    stop_poll_.store(true);
}

bool network_manager::stop_requested() const
{
    return stop_poll_.load();
}

std::filesystem::path network_manager::resolve_server_executable()
{
    auto p1 = std::filesystem::current_path() / "server.exe";
    if (std::filesystem::exists(p1))
        return p1;
    if (auto p2 = std::filesystem::current_path() / "cmake-build-release" / "server.exe"; std::filesystem::exists(p2))
        return p2;
    if (auto p3 = std::filesystem::current_path() / "cmake-build-debug" / "server.exe"; std::filesystem::exists(p3))
        return p3;
    return p1;
}

void network_manager::finish_connect_io()
{
    connecting_io_.store(nullptr);
}

void network_manager::finish_connect_attempt(const bool reset_session_if_failed)
{
    connecting_.store(false);
    bool failed = false;
    {
        std::lock_guard lock(mutex_);
        if (!connected_)
        {
            state_ = connection_state::disconnected;
            failed = true;
        }
    }
    if (failed && reset_session_if_failed && session_)
        session_->clear_joining_state();
}

void network_manager::close_sockets() const
{
    if (io_context_)
        io_context_->stop();
    if (tcp_socket_)
        tcp_socket_->close();
    if (ws_socket_)
        ws_socket_->close();
}

void network_manager::stop_connect_thread()
{
    stop_poll_.store(true);
    if (auto* io = connecting_io_.load())
        io->stop();
    if (connect_thread_.joinable())
        connect_thread_.join();
}

void network_manager::async_connect_to_server()
{
    if (connecting_.exchange(true))
        return;

    stop_poll_.store(false);
    stop_connect_thread();
    set_status_("Connecting...");
    {
        std::lock_guard lock(mutex_);
        state_ = connection_state::connecting;
    }

    connect_thread_ = std::thread([this]() {
        try
        {
            if (const auto res = connect_to_server(); !res && !stop_poll_.load())
                set_status_("Connection failed: " + res.message);
        }
        catch (const std::exception& ex)
        {
            if (!stop_poll_.load())
                set_status_("Connection error: " + std::string(ex.what()));
        }
        catch (...)
        {
            if (!stop_poll_.load())
                set_status_("Connection error");
        }
        finish_connect_attempt(false);
    });
}

void network_manager::async_tcp_discover_and_join(const uint32_t session_id)
{
    if (connecting_.exchange(true))
        return;

    stop_poll_.store(false);
    stop_connect_thread();
    set_status_("Discovering host for session #" + std::to_string(session_id) + " via UDP...");
    {
        std::lock_guard lock(mutex_);
        state_ = connection_state::connecting;
    }
    if (session_)
        session_->prepare_join(session_id);

    connect_thread_ = std::thread([this, session_id]() {
        try
        {
            const auto disc = udp_discovery::discover_host(session_id, std::chrono::milliseconds(3000));
            if (!disc)
            {
                if (!stop_poll_.load())
                    set_status_("UDP Discovery failed: " + disc.message);
                finish_connect_attempt(true);
                return;
            }

            const auto& [host_ip, tcp_port] = disc.value;
            set_endpoint(host_ip, std::to_string(tcp_port), connection_protocol::tcp);

            if (!stop_poll_.load())
                set_status_("Found host at " + host_ip + ":" + std::to_string(tcp_port) + ", connecting...");

            if (const auto res = connect_to_server(); !res)
            {
                if (!stop_poll_.load())
                    set_status_("TCP connection failed: " + res.message);
                finish_connect_attempt(true);
                return;
            }

            if (!stop_poll_.load())
                set_status_("Joining session #" + std::to_string(session_id) + "...");

            if (session_)
                session_->send_join_request(session_id);
        }
        catch (const std::exception& ex)
        {
            if (!stop_poll_.load())
                set_status_("Discovery error: " + std::string(ex.what()));
        }
        catch (...)
        {
            if (!stop_poll_.load())
                set_status_("Discovery error");
        }
        finish_connect_attempt(true);
    });
}

void network_manager::async_ws_connect_and_join(const uint32_t session_id)
{
    if (connecting_.exchange(true))
        return;

    stop_poll_.store(false);
    stop_connect_thread();
    set_status_("Connecting to WebSocket server...");
    {
        std::lock_guard lock(mutex_);
        state_ = connection_state::connecting;
    }
    if (session_)
        session_->prepare_join(session_id);

    connect_thread_ = std::thread([this, session_id]() {
        try
        {
            if (const auto res = connect_to_server(); !res)
            {
                if (!stop_poll_.load())
                    set_status_("WebSocket connection failed: " + res.message);
                finish_connect_attempt(true);
                return;
            }

            if (!stop_poll_.load())
                set_status_("Joining session #" + std::to_string(session_id) + "...");

            if (session_)
                session_->send_join_request(session_id);
        }
        catch (const std::exception& ex)
        {
            if (!stop_poll_.load())
                set_status_("Connection error: " + std::string(ex.what()));
        }
        catch (...)
        {
            if (!stop_poll_.load())
                set_status_("Connection error");
        }
        finish_connect_attempt(true);
    });
}

void network_manager::async_ws_connect_and_create()
{
    if (connecting_.exchange(true))
        return;

    stop_poll_.store(false);
    stop_connect_thread();
    set_status_("Connecting to WebSocket server...");
    {
        std::lock_guard lock(mutex_);
        state_ = connection_state::connecting;
    }
    if (session_)
        session_->prepare_create();

    connect_thread_ = std::thread([this]() {
        try
        {
            if (const auto res = connect_to_server(); !res)
            {
                if (!stop_poll_.load())
                    set_status_("WebSocket connection failed: " + res.message);
                finish_connect_attempt(true);
                return;
            }

            if (!stop_poll_.load())
                set_status_("Creating session...");

            if (session_)
                session_->send_create_request();
        }
        catch (const std::exception& ex)
        {
            if (!stop_poll_.load())
                set_status_("Connection error: " + std::string(ex.what()));
        }
        catch (...)
        {
            if (!stop_poll_.load())
                set_status_("Connection error");
        }
        finish_connect_attempt(true);
    });
}

void network_manager::async_start_local_server()
{
    if (connecting_.exchange(true))
        return;

    stop_poll_.store(false);
    stop_connect_thread();
    set_status_("Starting local server...");
    {
        std::lock_guard lock(mutex_);
        state_ = connection_state::connecting;
    }

    connect_thread_ = std::thread([this]() {
        try
        {
            if (const auto res = start_local_server(); !res && !stop_poll_.load())
                set_status_("Server start failed: " + res.message);
        }
        catch (const std::exception& ex)
        {
            if (!stop_poll_.load())
                set_status_("Server start error: " + std::string(ex.what()));
        }
        catch (...)
        {
            if (!stop_poll_.load())
                set_status_("Server start error");
        }
        finish_connect_attempt(false);
    });
}

result<bool> network_manager::start_local_server()
{
    if (local_server_.running())
        return {.value = false, .err = ::error::rejected, .message = "Already running"};
    const auto server_exe = resolve_server_executable();
    if (!std::filesystem::exists(server_exe))
        return {.value = false, .err = ::error::rejected, .message = "server.exe not found at " + server_exe.string()};

    stop_poll_.store(false);
    std::string port;
    connection_protocol proto;
    {
        std::lock_guard lock(mutex_);
        port = port_;
        proto = protocol_;
    }

    auto tcp_p = std::string(net_config::DEFAULT_TCP_PORT_STR);
    auto ws_p = std::string(net_config::DEFAULT_WS_PORT_STR);
    try
    {
        const int p = std::stoi(port);
        if (proto == connection_protocol::tcp)
        {
            tcp_p = port;
            ws_p = std::to_string(p == net_config::DEFAULT_WS_PORT ? net_config::DEFAULT_WS_PORT + 1 : net_config::DEFAULT_WS_PORT);
        }
        else
        {
            ws_p = port;
            tcp_p = std::to_string(p == net_config::DEFAULT_TCP_PORT ? net_config::DEFAULT_TCP_PORT + 1 : net_config::DEFAULT_TCP_PORT);
        }
    }
    catch (...)
    {
        tcp_p = std::string(net_config::DEFAULT_TCP_PORT_STR);
        ws_p = std::string(net_config::DEFAULT_WS_PORT_STR);
    }

    if (auto res = local_server_.start(server_exe, {"--tcp-port", tcp_p, "--ws-port", ws_p, "--udp-port", std::to_string(net_config::DEFAULT_UDP_PORT)}); !res)
        return res;

    result<bool> conn;
    for (int i = 0; i < 15; ++i)
    {
        if (stop_poll_.load() || !local_server_.running())
            break;
        conn = connect_to_server(std::chrono::milliseconds(500));
        if (conn)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!conn)
    {
        local_server_.stop(1);
        return conn;
    }
    if (session_)
        session_->create_session();
    return {.value = true, .err = error::none};
}

result<bool> network_manager::connect_to_server(const std::chrono::milliseconds timeout)
{
    stop_poll_.store(false);
    {
        std::lock_guard lock(mutex_);
        if (connected_)
            return {.value = false, .err = ::error::rejected, .message = "Already connected"};
    }
    if (session_ && session_->has_client())
        return {.value = false, .err = ::error::rejected, .message = "Already connected"};

    std::string host;
    std::string port;
    connection_protocol proto;
    {
        std::lock_guard lock(mutex_);
        host = host_;
        port = port_;
        proto = protocol_;
    }

    auto new_io = std::make_unique<net::io_context>();
    connecting_io_.store(new_io.get());
    result<bool> conn_res;

    std::unique_ptr<tcpSocket> new_tcp;
    std::unique_ptr<webSocket> new_ws;

    if (proto == connection_protocol::tcp)
    {
        new_tcp = std::make_unique<tcpSocket>(tcp_addr{.host = host, .port = port}, *new_io);
        conn_res = new_tcp->connect(timeout);
    }
    else
    {
        new_ws = std::make_unique<webSocket>(webaddr{.host = host, .port = port, .path = "/"}, *new_io);
        conn_res = new_ws->connect(timeout);
    }

    if (!conn_res || stop_poll_.load())
    {
        finish_connect_io();
        return conn_res;
    }

    std::unique_ptr<sessionClient> new_client;
    if (proto == connection_protocol::tcp)
        new_client = std::make_unique<sessionClient>(*new_tcp);
    else
        new_client = std::make_unique<sessionClient>(*new_ws);

    stop_poll_.store(true);
    close_sockets();
    if (session_)
        session_->join_poll_thread();

    {
        std::lock_guard lock(mutex_);
        io_context_ = std::move(new_io);
        tcp_socket_ = std::move(new_tcp);
        ws_socket_ = std::move(new_ws);
        connected_ = true;
        state_ = connection_state::connected;
    }

    if (session_)
    {
        session_->attach_client(std::move(new_client));
        stop_poll_.store(false);
        session_->start_poll();
    }
    else
    {
        stop_poll_.store(false);
    }

    finish_connect_io();
    set_status_("Connected to " + host);
    return {.value = true, .err = error::none};
}

void network_manager::handle_disconnect()
{
    stop_poll_.store(true);
    close_sockets();
    if (session_)
    {
        session_->reset_session();
        session_->detach_client();
    }

    std::lock_guard lock(mutex_);
    tcp_socket_.reset();
    ws_socket_.reset();
    io_context_.reset();
    connected_ = false;
    state_ = connection_state::disconnected;
}

void network_manager::disconnect()
{
    stop_connect_thread();
    if (session_)
        session_->send_leave_or_close();
    stop_poll_.store(true);
    close_sockets();
    if (session_)
        session_->join_poll_thread();
    handle_disconnect();
}

void network_manager::stop_local_server()
{
    disconnect();
    local_server_.stop();
    set_status_("Local server stopped");
}

void network_manager::shutdown()
{
    if (shut_down_.exchange(true))
        return;
    stop_connect_thread();
    stop_poll_.store(true);
    close_sockets();
    if (session_)
    {
        session_->join_poll_thread();
        session_->reset_session();
        session_->detach_client();
        session_ = nullptr;
    }
    tcp_socket_.reset();
    ws_socket_.reset();
    io_context_.reset();
    {
        std::lock_guard lock(mutex_);
        connected_ = false;
        state_ = connection_state::disconnected;
    }
    local_server_.stop();
}

void network_manager::set_disconnected()
{
    std::lock_guard lock(mutex_);
    connected_ = false;
    state_ = connection_state::disconnected;
}

void network_manager::set_endpoint(const std::string& host, const std::string& port, const connection_protocol protocol)
{
    std::lock_guard lock(mutex_);
    host_ = host;
    port_ = port;
    protocol_ = protocol;
}

bool network_manager::toggle_protocol()
{
    std::lock_guard lock(mutex_);
    if (connected_ || connecting_.load())
        return false;
    if (protocol_ == connection_protocol::tcp)
    {
        protocol_ = connection_protocol::websocket;
        if (port_ == net_config::DEFAULT_TCP_PORT_STR)
            port_ = std::string(net_config::DEFAULT_WS_PORT_STR);
    }
    else
    {
        protocol_ = connection_protocol::tcp;
        if (port_ == net_config::DEFAULT_WS_PORT_STR)
            port_ = std::string(net_config::DEFAULT_TCP_PORT_STR);
    }
    return true;
}

std::string& network_manager::host()
{
    return host_;
}

std::string& network_manager::port()
{
    return port_;
}

std::string network_manager::host_copy() const
{
    std::lock_guard lock(mutex_);
    return host_;
}

std::string network_manager::port_copy() const
{
    std::lock_guard lock(mutex_);
    return port_;
}

connection_protocol network_manager::protocol() const
{
    std::lock_guard lock(mutex_);
    return protocol_;
}

bool network_manager::connected() const
{
    std::lock_guard lock(mutex_);
    return connected_;
}

connection_state network_manager::state() const
{
    std::lock_guard lock(mutex_);
    return state_;
}

bool network_manager::is_connecting() const
{
    return connecting_.load();
}

bool network_manager::local_server_running() const
{
    return local_server_.running();
}
