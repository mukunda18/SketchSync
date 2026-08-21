#include "engine/session/session_manager.h"

#include <charconv>
#include <chrono>
#include <thread>

#include "engine/canvas/canvas.h"
#include "engine/file/file_manager.h"
#include "engine/network/network_manager.h"
#include "engine/session/sessionClient.h"

session_manager::session_manager(canvas& surface,
                                 file_manager& files,
                                 network_manager& net,
                                 std::atomic<bool>& dirty,
                                 std::function<void(std::string)> set_status)
    : surface_(surface),
      files_(files),
      net_(net),
      dirty_(dirty),
      set_status_(std::move(set_status))
{
}

session_manager::~session_manager()
{
    join_poll_thread();
}

void session_manager::join_session()
{
    uint32_t session_id = 0;
    {
        std::lock_guard lock(session_mutex_);
        const auto [ptr, ec] = std::from_chars(session_id_input_.data(),
                                               session_id_input_.data() + session_id_input_.size(),
                                               session_id);
        if (ec != std::errc{} || session_id == 0)
        {
            set_status_("Invalid session ID");
            return;
        }
        if (in_session_)
        {
            set_status_("Already in a session. Leave current session first.");
            return;
        }
    }

    if (net_.connected() && has_client())
    {
        send_join_request(session_id);
        return;
    }

    if (net_.protocol() == connection_protocol::tcp)
        net_.async_tcp_discover_and_join(session_id);
    else
        net_.async_ws_connect_and_join(session_id);
}

void session_manager::create_session()
{
    {
        std::lock_guard lock(session_mutex_);
        if (in_session_)
        {
            set_status_("Already in a session. Leave current session first.");
            return;
        }
    }

    if (net_.connected() && has_client())
    {
        send_create_request();
        return;
    }

    if (net_.protocol() == connection_protocol::websocket)
        net_.async_ws_connect_and_create();
    else
        set_status_("Start Local server to host locally, or Join a session");
}

void session_manager::prepare_join(const uint32_t session_id)
{
    std::lock_guard lock(session_mutex_);
    joining_state_ = session_joining_state::joining;
    session_id_ = session_id;
}

void session_manager::prepare_create()
{
    std::lock_guard lock(session_mutex_);
    joining_state_ = session_joining_state::creating;
}

void session_manager::send_join_request(const uint32_t session_id)
{
    std::lock_guard lock(session_mutex_);
    if (!client_)
        return;
    if (const auto res = client_->send_join(session_id, "SketchSync"); !res)
    {
        set_status_(res.message);
        return;
    }
    joining_state_ = session_joining_state::joining;
    session_id_ = session_id;
    set_status_("Joining session #" + std::to_string(session_id) + "...");
}

void session_manager::send_create_request()
{
    std::lock_guard lock(session_mutex_);
    if (!client_)
        return;
    if (const auto res = client_->send_create("SketchSync"); !res)
    {
        set_status_(res.message);
        return;
    }
    joining_state_ = session_joining_state::creating;
    set_status_("Creating session...");
}

void session_manager::leave_session()
{
    send_leave_or_close();
    reset_session();
    set_status_("Left session");
}

void session_manager::reset_session()
{
    std::lock_guard lock(session_mutex_);
    if (client_)
        client_->mark_session_closed();
    in_session_ = false;
    session_id_ = 0;
    member_id_ = 0;
    is_host_ = false;
    joining_state_ = session_joining_state::none;
}

void session_manager::clear_joining_state()
{
    std::lock_guard lock(session_mutex_);
    joining_state_ = session_joining_state::none;
}

void session_manager::attach_client(std::unique_ptr<sessionClient> client)
{
    std::lock_guard lock(session_mutex_);
    client_ = std::move(client);
}

void session_manager::detach_client()
{
    std::lock_guard lock(session_mutex_);
    client_.reset();
}

void session_manager::start_poll()
{
    join_poll_thread();
    poll_thread_ = std::thread(&session_manager::poll_session, this);
}

void session_manager::join_poll_thread()
{
    if (poll_thread_.joinable())
        poll_thread_.join();
}

bool session_manager::has_client() const
{
    std::lock_guard lock(session_mutex_);
    return static_cast<bool>(client_);
}

void session_manager::send_leave_or_close() const
{
    sessionClient* client = nullptr;
    bool in_session = false;
    bool host = false;
    {
        std::lock_guard lock(session_mutex_);
        client = client_.get();
        in_session = in_session_;
        host = is_host_;
    }
    if (client && in_session)
    {
        if (host)
            client->send_close_session();
        else
            client->send_leave();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void session_manager::broadcast_draw(const draw_operation& op, const bool track_pending)
{
    sessionClient* client = nullptr;
    bool in_session = false;
    {
        std::lock_guard lock(session_mutex_);
        client = client_.get();
        in_session = in_session_ || (client && client->in_session());
    }
    if (!client || !in_session)
        return;
    if (track_pending)
    {
        std::lock_guard lock(pending_mutex_);
        pending_operations_.insert(op.operation_id);
    }
    client->send_draw(op);
}

uint32_t session_manager::member_id_or(const uint32_t fallback) const
{
    std::lock_guard lock(session_mutex_);
    if (client_)
        return client_->member_id();
    return fallback;
}

bool session_manager::host_owns_canvas() const
{
    std::lock_guard lock(session_mutex_);
    return !client_ || client_->is_host();
}

bool session_manager::in_session() const
{
    std::lock_guard lock(session_mutex_);
    return in_session_;
}

bool session_manager::is_host() const
{
    std::lock_guard lock(session_mutex_);
    return is_host_;
}

uint32_t session_manager::session_id() const
{
    std::lock_guard lock(session_mutex_);
    return session_id_;
}

uint32_t session_manager::member_id() const
{
    std::lock_guard lock(session_mutex_);
    return member_id_;
}

std::string& session_manager::session_id_input()
{
    return session_id_input_;
}

uint64_t session_manager::next_operation_id()
{
    const uint32_t member = member_id_or(1);
    return (static_cast<uint64_t>(member) << 32) | next_operation_number_.fetch_add(1);
}

void session_manager::poll_session()
{
    while (!net_.stop_requested())
    {
        sessionClient* client = nullptr;
        {
            std::lock_guard lock(session_mutex_);
            client = client_.get();
        }
        if (!client)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        const auto msg_res = client->poll();
        if (!msg_res)
        {
            if (!net_.stop_requested())
            {
                set_status_(std::string("Disconnected: ") + msg_res.message);
                reset_session();
                net_.set_disconnected();
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
            if (client->is_host())
                client->send_canvas_state(surface_.snapshot());
            break;
        default: break;
        }
    }
}

void session_manager::handle_notification(const std::vector<uint8_t>& payload)
{
    if (payload.empty())
        return;
    switch (payload[0])
    {
    case notifcode::MEMBER_JOINED: set_status_("A member joined"); break;
    case notifcode::MEMBER_LEFT: set_status_("A member left"); break;
    case notifcode::SESSION_CLOSED: reset_session(); set_status_("Session closed by host"); break;
    default: break;
    }
}

void session_manager::handle_draw(const std::vector<uint8_t>& payload)
{
    const auto op_res = parseDrawOperation(payload);
    if (!op_res)
        return;

    draw_operation op = op_res.value;
    {
        std::lock_guard lock(pending_mutex_);
        pending_operations_.erase(op.operation_id);
    }

    sessionClient* client = nullptr;
    {
        std::lock_guard lock(session_mutex_);
        client = client_.get();
    }
    if (!client)
        return;

    if (client->is_host())
    {
        if (!validateDrawOperation(op, false))
            return;
        if (op.operation_id != 0 && surface_.contains_operation(op.operation_id))
            return;
        op.seq = surface_.apply(op);
        files_.enqueue_if_auto_save(op);
        client->send_draw_raw(op);
    }
    else
    {
        if (op.seq == 0)
            return;
        const uint32_t expected = surface_.next_sequence();
        if (op.seq > expected)
        {
            client->request_canvas_state();
            return;
        }
        if (op.seq < expected)
            return;
        op.seq = surface_.apply(op);
        files_.enqueue_if_auto_save(op);
    }
    dirty_.store(true);
}

void session_manager::handle_canvas_state(const std::vector<uint8_t>& payload) const
{
    if (const auto state_res = parseCanvasStateMessage(payload))
    {
        surface_.load(state_res.value.operations);
        dirty_.store(true);
        set_status_("Canvas synchronized");
        files_.mark_synced_for_save();
    }
}

void session_manager::handle_ack(const Message& msg)
{
    std::lock_guard lock(session_mutex_);
    switch (joining_state_)
    {
    case session_joining_state::creating:
    {
        if (const auto ack = parseCreateAckMessage(msg.payload); ack && ack.value.ack_code == ackcode::CREATE_OK)
        {
            if (client_)
                client_->set_session_info(ack.value.member_id, ack.value.session_id, true);
            member_id_ = ack.value.member_id;
            session_id_ = ack.value.session_id;
            is_host_ = true;
            in_session_ = true;
            joining_state_ = session_joining_state::in_session;
            set_status_("Hosting session #" + std::to_string(ack.value.session_id));
        }
        break;
    }
    case session_joining_state::joining:
    {
        if (const auto ack = parseJoinAckMessage(msg.payload); ack && ack.value.ack_code == ackcode::JOIN_OK)
        {
            if (client_)
                client_->set_session_info(ack.value.member_id, session_id_, false);
            member_id_ = ack.value.member_id;
            is_host_ = false;
            in_session_ = true;
            joining_state_ = session_joining_state::in_session;
            if (client_)
                client_->request_canvas_state();
            set_status_("Joined session #" + std::to_string(session_id_));
        }
        break;
    }
    case session_joining_state::leaving:
    case session_joining_state::closing:
        if (client_)
            client_->mark_session_closed();
        in_session_ = false;
        session_id_ = 0;
        member_id_ = 0;
        is_host_ = false;
        joining_state_ = session_joining_state::none;
        break;
    default:
        break;
    }
}

void session_manager::handle_error(const std::vector<uint8_t>& payload)
{
    if (const auto err = parseErrorMessage(payload))
    {
        set_status_(err.value.err_message);
        std::lock_guard lock(session_mutex_);
        joining_state_ = session_joining_state::none;
    }
}
