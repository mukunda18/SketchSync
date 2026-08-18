#include "engine/engine.h"

#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <utility>

#include "engine/client/sessionClient.h"
#include "engine/process/server_process.h"
#include "engine/persistence/persistenceWriter.h"
#include "engine/ui/file_dialog.h"
#include "engine/ui/ui.h"
#include "common/network_constants.h"

sketch_app::sketch_app() : layout()
{
    surface.create(ui::WINDOW_WIDTH, ui::WINDOW_HEIGHT - ui::TOP_BAR_HEIGHT, ui::BACKGROUND_COLOR);
}

sketch_app::~sketch_app()
{
    stop_connect_thread();
    stop_local_server();
    if (canvas_texture.id != 0)
        UnloadTexture(canvas_texture);
}

void sketch_app::rebuild_render_texture()
{
    if (canvas_texture.id == 0)
        return;
    ui::sync_texture(canvas_texture, surface.copy_pixels(), upload_buffer);
}

std::filesystem::path sketch_app::resolve_server_executable()
{
    auto p1 = std::filesystem::current_path() / "server.exe";
    if (std::filesystem::exists(p1)) return p1;
    if (auto p2 = std::filesystem::current_path() / "cmake-build-release" / "server.exe"; std::filesystem::exists(p2)) return p2;
    if (auto p3 = std::filesystem::current_path() / "cmake-build-debug" / "server.exe"; std::filesystem::exists(p3)) return p3;
    return p1;
}

void sketch_app::set_status(std::string value)
{
    std::lock_guard lock(status_mutex);
    status = std::move(value);
}

std::string sketch_app::get_status() const
{
    std::lock_guard lock(status_mutex);
    return status;
}

void sketch_app::open_and_load()
{
    if (const auto path = ui::open_binary_file_dialog(); path.has_value())
    {
        current_file = path->string();
        std::string load_status;
        if (!ui::load_binary_replay(path.value(), surface, load_status))
            current_file = "untitled";
        else
        {
            operation_log.reset();
            operation_log = std::make_unique<persistence_writer>(path->string());
        }
        set_status(std::move(load_status));
        active_stroke.reset();
        dirty.store(true);
    }
}

void sketch_app::join_session()
{
    uint32_t session_id = 0;
    {
        std::lock_guard lock(net_mutex);
        const auto [ptr, ec] = std::from_chars(net_state.session.session_id_input.data(),
                                               net_state.session.session_id_input.data() + net_state.session.session_id_input.size(),
                                               session_id);
        if (ec != std::errc{} || session_id == 0)
        {
            set_status("Invalid session ID");
            return;
        }

        if (net_state.session.in_session)
        {
            set_status("Already in a session. Leave current session first.");
            return;
        }

        if (net_state.net.connected && session_client)
        {
            if (const auto res = session_client->send_join(session_id, "SketchSync"); !res)
            {
                set_status(res.message);
            }
            else
            {
                net_state.session_state = session_joining_state::joining;
                net_state.session.session_id = session_id;
                set_status("Joining session #" + std::to_string(session_id) + "...");
            }
            return;
        }
    }

    if (net_state.net.protocol == connection_protocol::tcp)
    {
        async_tcp_discover_and_join(session_id);
    }
    else
    {
        async_ws_connect_and_join(session_id);
    }
}

void sketch_app::create_session()
{
    {
        std::lock_guard lock(net_mutex);
        if (net_state.session.in_session)
        {
            set_status("Already in a session. Leave current session first.");
            return;
        }

        if (net_state.net.connected && session_client)
        {
            if (const auto res = session_client->send_create("SketchSync"); !res)
            {
                set_status(res.message);
            }
            else
            {
                net_state.session_state = session_joining_state::creating;
                set_status("Creating session...");
            }
            return;
        }
    }

    if (net_state.net.protocol == connection_protocol::websocket)
    {
        async_ws_connect_and_create();
    }
    else
    {
        set_status("Start Local server to host locally, or Join a session");
    }
}

void sketch_app::leave_session()
{
    if (session_client && net_state.session.in_session) {
        if (session_client->is_host()) session_client->send_close_session();
        else session_client->send_leave();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    handle_leave();
    set_status("Left session");
}

void sketch_app::handle_leave()
{
    std::lock_guard lock(net_mutex);
    if (session_client) session_client->mark_session_closed();
    net_state.session.in_session = false;
    net_state.session.session_id = 0;
    net_state.session.member_id = 0;
    net_state.session.is_host = false;
    net_state.session_state = session_joining_state::none;
}

void sketch_app::clear_canvas()
{
    active_stroke.reset();
    const uint32_t member = session_client ? session_client->member_id() : 1;
    const uint64_t operation_id = (static_cast<uint64_t>(member) << 32) | next_operation_number.fetch_add(1);
    const bool host_owns_canvas = !session_client || session_client->is_host();

    draw_operation clear_op;
    clear_op.operation_id = operation_id;
    clear_op.member_id = member;
    clear_op.tool = tool_type::clear;
    clear_op.color = 0xFFFFFFFF;
    clear_op.thickness = 1;

    if (host_owns_canvas) {
        surface.apply(clear_op);
        if (operation_log) operation_log->enqueue(clear_op);
        rebuild_render_texture();
        dirty.store(true);
    }

    if (session_client && session_client->in_session()) {
        if (!host_owns_canvas) {
            std::lock_guard lock(pending_mutex);
            pending_operations.insert(clear_op.operation_id);
        }
        session_client->send_draw(clear_op);
    }
    set_status("Canvas cleared");
}

void sketch_app::process_canvas_input(const canvas_input_state& input)
{
    const uint32_t member = session_client ? session_client->member_id() : 1;
    const uint64_t operation_id = (static_cast<uint64_t>(member) << 32) | next_operation_number.fetch_add(1);
    const bool host_owns_canvas = !session_client || session_client->is_host();
    const auto committed = ::process_canvas_input(surface, active_stroke, input, operation_id, member, active_color, active_thickness, host_owns_canvas, active_tool);
    if (!committed) return;

    if (host_owns_canvas && operation_log) operation_log->enqueue(*committed);
    dirty.store(true);
    if (session_client && session_client->in_session()) {
        if (!host_owns_canvas) {
            std::lock_guard lock(pending_mutex);
            pending_operations.insert(committed->operation_id);
        }
        session_client->send_draw(*committed);
    }
}

void sketch_app::poll_session()
{
    while (!stop_poll.load())
    {
        if (!session_client) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }
        const auto msg_res = session_client->poll();
        if (!msg_res) {
            if (!stop_poll.load()) {
                set_status(std::string("Disconnected: ") + msg_res.message);
                handle_leave();
                std::lock_guard lock(net_mutex);
                net_state.net.connected = false;
                net_state.state = connection_state::disconnected;
            }
            break;
        }

        switch (const auto& msg = msg_res.value; msg.header.opcode)
        {
        case Opcode::NOTIFICATION: handle_notification(msg.payload); break;
        case Opcode::DRAW: handle_draw(msg.payload); break;
        case Opcode::CANVAS_STATE: handle_canvas_state(msg.payload); break;
        case Opcode::ACK: handle_ack(msg); break;
        case Opcode::ERROR_MSG: handle_error(msg.payload); break;
        case Opcode::CANVAS_STATE_REQUEST:
            if (session_client && session_client->is_host()) {
                session_client->send_canvas_state(surface.snapshot());
            }
            break;
        default: break;
        }
    }
}

void sketch_app::handle_notification(const std::vector<uint8_t>& payload)
{
    if (payload.empty()) return;
    switch (payload[0])
    {
    case notifcode::MEMBER_JOINED: set_status("A member joined"); break;
    case notifcode::MEMBER_LEFT: set_status("A member left"); break;
    case notifcode::SESSION_CLOSED: handle_leave(); set_status("Session closed by host"); break;
    default: break;
    }
}

void sketch_app::handle_draw(const std::vector<uint8_t>& payload)
{
    const auto op_res = parseDrawOperation(payload);
    if (!op_res) return;

    draw_operation op = op_res.value;
    { std::lock_guard lock(pending_mutex); pending_operations.erase(op.operation_id); }
    if (session_client->is_host()) {
        if (!validateDrawOperation(op, false)) return;
        if (op.operation_id != 0 && surface.contains_operation(op.operation_id)) return;
        op.seq = surface.apply(op);
        if (operation_log) operation_log->enqueue(op);
        session_client->send_draw_raw(op);
    } else {
        if (op.seq == 0) return;
        const uint32_t expected = surface.next_sequence();
        if (op.seq > expected) { session_client->request_canvas_state(); return; }
        if (op.seq < expected) return;
        op.seq = surface.apply(op);
    }
    dirty.store(true);
}

void sketch_app::handle_canvas_state(const std::vector<uint8_t>& payload)
{
    if (const auto state_res = parseCanvasStateMessage(payload)) { surface.load(state_res.value.operations); dirty.store(true); set_status("Canvas synchronized"); }
}

void sketch_app::handle_ack(const Message& msg)
{
    std::unique_lock lock(net_mutex);
    switch (net_state.session_state)
    {
    case session_joining_state::creating: {
        if (const auto ack = parseCreateAckMessage(msg.payload); ack && ack.value.ack_code == ackcode::CREATE_OK) {
            session_client->set_session_info(ack.value.member_id, ack.value.session_id, true);
            net_state.session.member_id = ack.value.member_id; net_state.session.session_id = ack.value.session_id;
            net_state.session.is_host = true; net_state.session.in_session = true;
            net_state.session_state = session_joining_state::in_session;
            set_status("Hosting session #" + std::to_string(ack.value.session_id));
        }
        break;
    }
    case session_joining_state::joining: {
        if (const auto ack = parseJoinAckMessage(msg.payload); ack && ack.value.ack_code == ackcode::JOIN_OK) {
            session_client->set_session_info(ack.value.member_id, net_state.session.session_id, false);
            net_state.session.member_id = ack.value.member_id; net_state.session.is_host = false;
            net_state.session.in_session = true; net_state.session_state = session_joining_state::in_session;
            session_client->request_canvas_state();
            set_status("Joined session #" + std::to_string(net_state.session.session_id));
        }
        break;
    }
    case session_joining_state::leaving:
    case session_joining_state::closing: lock.unlock(); handle_leave(); break;
    default: break;
    }
}

void sketch_app::handle_error(const std::vector<uint8_t>& payload)
{
    if (const auto err = parseErrorMessage(payload)) { set_status(err.value.err_message); std::lock_guard lock(net_mutex); net_state.session_state = session_joining_state::none; }
}

void sketch_app::stop_connect_thread()
{
    stop_poll.store(true);
    if (auto* io = connecting_io.load())
    {
        io->stop();
    }
    if (connect_thread.joinable())
    {
        connect_thread.join();
    }
}

void sketch_app::async_connect_to_server()
{
    if (connecting.exchange(true))
        return;

    stop_poll.store(false);
    stop_connect_thread();
    set_status("Connecting...");
    {
        std::lock_guard lock(net_mutex);
        net_state.state = connection_state::connecting;
    }

    connect_thread = std::thread([this]() {
        struct Guard {
            sketch_app* app;
            ~Guard() {
                app->connecting.store(false);
                std::lock_guard lock(app->net_mutex);
                if (!app->net_state.net.connected)
                    app->net_state.state = connection_state::disconnected;
            }
        } guard{this};
        try {
            const auto res = connect_to_server();
            if (!res && !stop_poll.load())
            {
                set_status("Connection failed: " + res.message);
            }
        } catch (const std::exception& ex) {
            if (!stop_poll.load()) set_status("Connection error: " + std::string(ex.what()));
        } catch (...) {
            if (!stop_poll.load()) set_status("Connection error");
        }
    });
}

void sketch_app::async_tcp_discover_and_join(const uint32_t session_id)
{
    if (connecting.exchange(true))
        return;

    stop_poll.store(false);
    stop_connect_thread();
    set_status("Discovering host for session #" + std::to_string(session_id) + " via UDP...");
    {
        std::lock_guard lock(net_mutex);
        net_state.state = connection_state::connecting;
        net_state.session_state = session_joining_state::joining;
        net_state.session.session_id = session_id;
    }

    connect_thread = std::thread([this, session_id]() {
        struct Guard {
            sketch_app* app;
            ~Guard() {
                app->connecting.store(false);
                std::lock_guard lock(app->net_mutex);
                if (!app->net_state.net.connected) {
                    app->net_state.state = connection_state::disconnected;
                    app->net_state.session_state = session_joining_state::none;
                }
            }
        } guard{this};

        try {
            const auto disc = udp_discovery::discover_host(session_id, std::chrono::milliseconds(3000));
            if (!disc)
            {
                if (!stop_poll.load())
                    set_status("UDP Discovery failed: " + disc.message);
                return;
            }

            const auto& [host_ip, tcp_port] = disc.value;
            {
                std::lock_guard lock(net_mutex);
                net_state.net.host = host_ip;
                net_state.net.port = std::to_string(tcp_port);
                net_state.net.protocol = connection_protocol::tcp;
            }

            if (!stop_poll.load())
                set_status("Found host at " + host_ip + ":" + std::to_string(tcp_port) + ", connecting...");

            const auto res = connect_to_server();
            if (!res)
            {
                if (!stop_poll.load())
                    set_status("TCP connection failed: " + res.message);
                return;
            }

            if (!stop_poll.load())
                set_status("Joining session #" + std::to_string(session_id) + "...");

            if (session_client)
            {
                const auto join_res = session_client->send_join(session_id, "SketchSync");
                if (!join_res && !stop_poll.load())
                {
                    set_status("Join request failed: " + join_res.message);
                }
            }
        }
        catch (const std::exception& ex) {
            if (!stop_poll.load()) set_status("Discovery error: " + std::string(ex.what()));
        }
        catch (...) {
            if (!stop_poll.load()) set_status("Discovery error");
        }
    });
}

void sketch_app::async_ws_connect_and_join(const uint32_t session_id)
{
    if (connecting.exchange(true))
        return;

    stop_poll.store(false);
    stop_connect_thread();
    set_status("Connecting to WebSocket server...");
    {
        std::lock_guard lock(net_mutex);
        net_state.state = connection_state::connecting;
        net_state.session_state = session_joining_state::joining;
        net_state.session.session_id = session_id;
    }

    connect_thread = std::thread([this, session_id]() {
        struct Guard {
            sketch_app* app;
            ~Guard() {
                app->connecting.store(false);
                std::lock_guard lock(app->net_mutex);
                if (!app->net_state.net.connected) {
                    app->net_state.state = connection_state::disconnected;
                    app->net_state.session_state = session_joining_state::none;
                }
            }
        } guard{this};

        try {
            const auto res = connect_to_server();
            if (!res)
            {
                if (!stop_poll.load())
                    set_status("WebSocket connection failed: " + res.message);
                return;
            }

            if (!stop_poll.load())
                set_status("Joining session #" + std::to_string(session_id) + "...");

            if (session_client)
            {
                const auto join_res = session_client->send_join(session_id, "SketchSync");
                if (!join_res && !stop_poll.load())
                {
                    set_status("Join request failed: " + join_res.message);
                }
            }
        }
        catch (const std::exception& ex) {
            if (!stop_poll.load()) set_status("Connection error: " + std::string(ex.what()));
        }
        catch (...) {
            if (!stop_poll.load()) set_status("Connection error");
        }
    });
}

void sketch_app::async_ws_connect_and_create()
{
    if (connecting.exchange(true))
        return;

    stop_poll.store(false);
    stop_connect_thread();
    set_status("Connecting to WebSocket server...");
    {
        std::lock_guard lock(net_mutex);
        net_state.state = connection_state::connecting;
        net_state.session_state = session_joining_state::creating;
    }

    connect_thread = std::thread([this]() {
        struct Guard {
            sketch_app* app;
            ~Guard() {
                app->connecting.store(false);
                std::lock_guard lock(app->net_mutex);
                if (!app->net_state.net.connected) {
                    app->net_state.state = connection_state::disconnected;
                    app->net_state.session_state = session_joining_state::none;
                }
            }
        } guard{this};

        try {
            const auto res = connect_to_server();
            if (!res)
            {
                if (!stop_poll.load())
                    set_status("WebSocket connection failed: " + res.message);
                return;
            }

            if (!stop_poll.load())
                set_status("Creating session...");

            if (session_client)
            {
                const auto create_res = session_client->send_create("SketchSync");
                if (!create_res && !stop_poll.load())
                {
                    set_status("Create request failed: " + create_res.message);
                }
            }
        }
        catch (const std::exception& ex) {
            if (!stop_poll.load()) set_status("Connection error: " + std::string(ex.what()));
        }
        catch (...) {
            if (!stop_poll.load()) set_status("Connection error");
        }
    });
}

void sketch_app::async_start_local_server()
{
    if (connecting.exchange(true))
        return;

    stop_poll.store(false);
    stop_connect_thread();
    set_status("Starting local server...");
    {
        std::lock_guard lock(net_mutex);
        net_state.state = connection_state::connecting;
    }

    connect_thread = std::thread([this]() {
        struct Guard {
            sketch_app* app;
            ~Guard() {
                app->connecting.store(false);
                std::lock_guard lock(app->net_mutex);
                if (!app->net_state.net.connected)
                    app->net_state.state = connection_state::disconnected;
            }
        } guard{this};
        try {
            const auto res = start_local_server();
            if (!res && !stop_poll.load())
            {
                set_status("Server start failed: " + res.message);
            }
        } catch (const std::exception& ex) {
            if (!stop_poll.load()) set_status("Server start error: " + std::string(ex.what()));
        } catch (...) {
            if (!stop_poll.load()) set_status("Server start error");
        }
    });
}

result<bool> sketch_app::start_local_server()
{
    if (local_server.running()) return {.value = false, .err = ::error::rejected, .message = "Already running"};
    const auto server_exe = resolve_server_executable();
    if (!std::filesystem::exists(server_exe)) return {.value = false, .err = ::error::rejected, .message = "server.exe not found at " + server_exe.string()};

    stop_poll.store(false);
    std::string port;
    connection_protocol proto;
    {
        std::lock_guard lock(net_mutex);
        port = net_state.net.port;
        proto = net_state.net.protocol;
    }

    std::string tcp_p = std::string(net_config::DEFAULT_TCP_PORT_STR);
    std::string ws_p = std::string(net_config::DEFAULT_WS_PORT_STR);
    try {
        const int p = std::stoi(port);
        if (proto == connection_protocol::tcp) {
            tcp_p = port;
            ws_p = std::to_string(p == net_config::DEFAULT_WS_PORT ? net_config::DEFAULT_WS_PORT + 1 : net_config::DEFAULT_WS_PORT);
        } else {
            ws_p = port;
            tcp_p = std::to_string(p == net_config::DEFAULT_TCP_PORT ? net_config::DEFAULT_TCP_PORT + 1 : net_config::DEFAULT_TCP_PORT);
        }
    } catch (...) {
        tcp_p = std::string(net_config::DEFAULT_TCP_PORT_STR);
        ws_p = std::string(net_config::DEFAULT_WS_PORT_STR);
    }

    if (auto res = local_server.start({.executable = server_exe, .arguments = {"--tcp-port", tcp_p, "--ws-port", ws_p, "--udp-port", std::to_string(net_config::DEFAULT_UDP_PORT)}}); !res) return res;

    result<bool> conn;
    for (int i = 0; i < 15; ++i)
    {
        if (stop_poll.load() || !local_server.running()) break;
        conn = connect_to_server(std::chrono::milliseconds(500));
        if (conn) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!conn) { local_server.stop(1); return conn; }
    create_session();
    return {.value = true, .err = error::none};
}

result<bool> sketch_app::connect_to_server(const std::chrono::milliseconds timeout)
{
    stop_poll.store(false);
    {
        std::lock_guard lock(net_mutex);
        if (session_client || net_state.net.connected)
            return {.value = false, .err = ::error::rejected, .message = "Already connected"};
    }

    std::string host;
    std::string port;
    connection_protocol proto;
    {
        std::lock_guard lock(net_mutex);
        host = net_state.net.host;
        port = net_state.net.port;
        proto = net_state.net.protocol;
    }

    auto new_io = std::make_unique<net::io_context>();
    struct IOCleanup {
        std::atomic<net::io_context*>& ref;
        IOCleanup(std::atomic<net::io_context*>& r, net::io_context* ptr) : ref(r) { ref.store(ptr); }
        ~IOCleanup() { ref.store(nullptr); }
    } io_cleanup(connecting_io, new_io.get());
    result<bool> conn_res;

    std::unique_ptr<tcpSocket> new_tcp;
    std::unique_ptr<webSocket> new_ws;

    if (proto == connection_protocol::tcp) {
        new_tcp = std::make_unique<tcpSocket>(tcp_addr{.host = host, .port = port}, *new_io);
        conn_res = new_tcp->connect(timeout);
    } else {
        new_ws = std::make_unique<webSocket>(webaddr{.host = host, .port = port, .path = "/"}, *new_io);
        conn_res = new_ws->connect(timeout);
    }

    if (!conn_res || stop_poll.load()) { return conn_res; }

    std::unique_ptr<sessionClient> new_client;
    if (proto == connection_protocol::tcp)
        new_client = std::make_unique<sessionClient>(*new_tcp);
    else
        new_client = std::make_unique<sessionClient>(*new_ws);

    stop_poll.store(true);
    if (io_context) io_context->stop();
    if (tcp_socket) tcp_socket->close();
    if (ws_socket) ws_socket->close();
    if (poll_thread.joinable()) {
        poll_thread.join();
    }

    {
        std::lock_guard lock(net_mutex);
        io_context = std::move(new_io);
        tcp_socket = std::move(new_tcp);
        ws_socket = std::move(new_ws);
        session_client = std::move(new_client);
        net_state.net.connected = true;
        net_state.state = connection_state::connected;
    }

    stop_poll.store(false);
    poll_thread = std::thread(&sketch_app::poll_session, this);
    set_status("Connected to " + host);
    return {.value = true, .err = error::none};
}

void sketch_app::handle_disconnect()
{
    stop_poll.store(true);
    if (io_context) io_context->stop();
    if (tcp_socket) tcp_socket->close();
    if (ws_socket) ws_socket->close();
    handle_leave();

    std::lock_guard lock(net_mutex);
    session_client.reset();
    tcp_socket.reset();
    ws_socket.reset();
    io_context.reset();
    net_state.net.connected = false;
    net_state.state = connection_state::disconnected;
}

void sketch_app::disconnect() {
    stop_connect_thread();
    if (session_client && net_state.session.in_session) {
        if (session_client->is_host()) session_client->send_close_session(); else session_client->send_leave();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    stop_poll.store(true);
    if (io_context) io_context->stop();
    if (tcp_socket) tcp_socket->close();
    if (ws_socket) ws_socket->close();
    if (poll_thread.joinable()) {
        poll_thread.join();
    }
    handle_disconnect();
}

void sketch_app::stop_local_server() {
    disconnect();
    local_server.stop();
    set_status("Local server stopped");
}

int sketch_app::run()
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(ui::WINDOW_WIDTH, ui::WINDOW_HEIGHT, "SketchSync");
    SetWindowMinSize(1024, 768);

    ui::default_font = LoadFontEx("../engine/ui/ClarityCity-Black.otf", 64, nullptr, 0);

    if (!IsFontValid(ui::default_font)) {
        set_status("Warning: ClarityCity-Black.otf not found at ../engine/ui/");
    }

    canvas_texture = LoadTextureFromImage(GenImageColor(static_cast<int>(surface.width),static_cast<int>(surface.height), WHITE));
    rebuild_render_texture();
    SetTargetFPS(60);

    // Binding text fields
    layout.host_field.value = &net_state.net.host;
    layout.port_field.value = &net_state.net.port;
    layout.session_id_field.value = &net_state.session.session_id_input;

    while (!WindowShouldClose())
    {
        const auto ww = static_cast<float>(GetScreenWidth()), wh = static_cast<float>(GetScreenHeight());
        connection_protocol current_proto;
        bool is_connected, is_running;
        {
            std::lock_guard lock(net_mutex);
            current_proto = net_state.net.protocol;
            is_connected = net_state.net.connected;
            is_running = local_server.running();
        }

        layout.update_layout(ww, wh, current_proto);

        // UI Logic Updates
        if (layout.open_btn.update()) open_and_load();
        if (layout.clear_btn.update()) clear_canvas();

        if (layout.connect_btn.update()) {
            if (is_connected) {
                disconnect();
            } else if (current_proto == connection_protocol::websocket) {
                async_connect_to_server();
            } else {
                set_status("Enter Session ID and click Join to discover host, or click Start Local");
            }
        }

        if (layout.local_server_btn.update()) {
            if (is_running) stop_local_server();
            else async_start_local_server();
        }

        if (layout.protocol_toggle.update()) {
            std::lock_guard lock(net_mutex);
            if (!net_state.net.connected && !connecting.load()) {
                if (net_state.net.protocol == connection_protocol::tcp) {
                    net_state.net.protocol = connection_protocol::websocket;
                    if (net_state.net.port == net_config::DEFAULT_TCP_PORT_STR) {
                        net_state.net.port = std::string(net_config::DEFAULT_WS_PORT_STR);
                    }
                } else {
                    net_state.net.protocol = connection_protocol::tcp;
                    if (net_state.net.port == net_config::DEFAULT_WS_PORT_STR) {
                        net_state.net.port = std::string(net_config::DEFAULT_TCP_PORT_STR);
                    }
                }
            } else {
                set_status("Disconnect before switching protocol");
            }
        }

        if (!net_state.session.in_session) {
            if (layout.join_btn.update()) join_session();
            if (layout.create_btn.update()) create_session();
        } else {
            if (layout.leave_btn.update()) leave_session();
        }

        if (current_proto == connection_protocol::websocket) {
            layout.host_field.update();
            layout.port_field.update();
        }
        layout.session_id_field.update();

        for (auto& tb : layout.tool_buttons) {
            tb.selected = (active_tool == static_cast<tool_type>(tb.tool_id));
            if (tb.update()) active_tool = static_cast<tool_type>(tb.tool_id);
        }

        for (size_t i = 0; i < layout.thickness_buttons.size(); ++i) {
            constexpr std::array<uint8_t, 4> thicknesses{1, 2, 4, 8};
            layout.thickness_buttons[i].active = (active_thickness == thicknesses[i]);
            if (layout.thickness_buttons[i].update()) active_thickness = thicknesses[i];
        }

        for (auto& s : layout.color_swatches) {
            s.selected = (active_color == s.color);
            if (s.update()) active_color = s.color;
        }

        // Canvas Input
        const float canvas_area_w = ww - layout.left_panel_width - layout.right_panel_width;
        const float canvas_area_h = wh - layout.top_bar_height - layout.bottom_panel_height;
        float cw = canvas_area_w * 0.95f, ch = cw / (static_cast<float>(surface.width)/static_cast<float>(surface.height));
        if (ch > canvas_area_h * 0.95f) { ch = canvas_area_h * 0.95f; cw = ch * (static_cast<float>(surface.width)/static_cast<float>(surface.height)); }
        const Rectangle canvas_rect = { .x = layout.left_panel_width + (canvas_area_w - cw) * 0.5f, .y = layout.top_bar_height + (canvas_area_h - ch) * 0.5f, .width = cw, .height = ch };

        if (!IsWindowMinimized()) {
            const Vector2 mouse = GetMousePosition();
            process_canvas_input({.inside = CheckCollisionPointRec(mouse, canvas_rect), .pressed = IsMouseButtonPressed(MOUSE_LEFT_BUTTON), .down = IsMouseButtonDown(MOUSE_LEFT_BUTTON), .released = IsMouseButtonReleased(MOUSE_LEFT_BUTTON), .position = ui::normalize_point(ui::clamp_to_canvas(mouse, canvas_rect), canvas_rect), .tool = active_tool});
        }

        if (dirty.load()) { rebuild_render_texture(); dirty.store(false); }
        if (active_stroke.has_value()) ui::sync_texture(canvas_texture, surface.copy_pixels_with_preview(*active_stroke), upload_buffer);

        BeginDrawing();
        ClearBackground(RAYWHITE);
        layout.draw(net_state, get_status(), current_file, is_running);

        DrawTexturePro(canvas_texture, {.x = 0,.y = 0,.width = static_cast<float>(canvas_texture.width), .height = static_cast<float>(canvas_texture.height)}, canvas_rect, {.x = 0,.y = 0}, 0, WHITE);
        DrawRectangleLinesEx(canvas_rect, 1, DARKGRAY);
        EndDrawing();
    }
    stop_local_server();
    if (IsFontValid(ui::default_font)) UnloadFont(ui::default_font);
    ui::close_window(); return 0;
}
