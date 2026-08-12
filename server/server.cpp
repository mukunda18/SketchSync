#include "server.h"
#include "common/protocol/message.h"
#include <ranges>

server::server(
    net::io_context& io,
    const unsigned short webSocket_port,
    const unsigned short tcp_server_port)
    : io_(io),
      tcp_server(io, tcp_server_port),
      webSocket_server(io, webSocket_port)
{
}

server::~server()
{
    shutdown();
}

void server::run()
{
    tcp_accept_thread = std::thread(&server::run_tcp_accept_loop, this);
    ws_accept_thread = std::thread(&server::run_ws_accept_loop, this);
}

void server::shutdown()
{
    accept_stop_flag.store(true);
    tcp_server.close();
    webSocket_server.close();

    if (tcp_accept_thread.joinable())
        tcp_accept_thread.join();
    if (ws_accept_thread.joinable())
        ws_accept_thread.join();

    std::lock_guard lock(clients_mutex);
    for (const auto& slot : client_threads)
    {
        slot->stop_flag.store(true);
    }
    for (const auto& slot : client_threads)
    {
        if (slot->thread.joinable())
            slot->thread.join();
    }
    client_threads.clear();
}

void server::run_tcp_accept_loop()
{
    while (!accept_stop_flag.load() && tcp_server.is_open())
    {
        auto result = tcp_server.accept();
        if (result.err != error::none)
            continue;

        auto slot = std::make_unique<client_thread>();
        auto* slot_ptr = slot.get();

        slot->thread = std::thread(
            &server::handle_tcp_client, this,
            std::ref(slot_ptr->stop_flag),
            std::move(result.value));

        std::lock_guard lock(clients_mutex);
        client_threads.push_back(std::move(slot));
    }
}

void server::run_ws_accept_loop()
{
    while (!accept_stop_flag.load() && webSocket_server.is_open())
    {
        auto result = webSocket_server.accept();
        if (result.err != error::none)
            continue;

        auto slot = std::make_unique<client_thread>();
        auto* slot_ptr = slot.get();

        slot->thread = std::thread(
            &server::handle_ws_client, this,
            std::ref(slot_ptr->stop_flag),
            std::move(result.value));

        std::lock_guard lock(clients_mutex);
        client_threads.push_back(std::move(slot));
    }
}

void server::handle_tcp_client(const std::atomic<bool>& stop_flag, tcp::socket raw_socket)
{
    tcpSocket client(std::move(raw_socket), io_);
    clientContext ctx;
    clientConnection conn{.tcp = &client};

    while (!stop_flag.load() && client.is_open())
    {
        auto header_result = client.receive(Header::SIZE);
        if (!header_result)
            break;

        auto parse_result = parseHeader(header_result.value);
        if (!parse_result)
            break;

        Header header = parse_result.value;

        auto payload_result = client.receive(header.length);
        if (!payload_result)
            break;

        dispatch(header, payload_result.value, ctx, conn);
    }

    client.close();

    if (ctx.member_id != 0)
    {
        unregister_connection(ctx.member_id);
        handle_leave(ctx);
    }
}

void server::handle_ws_client(const std::atomic<bool>& stop_flag, websocket_beast::stream<tcp::socket> raw_ws)
{
    webSocket client(std::move(raw_ws), io_);
    clientContext ctx;
    clientConnection conn{.ws = &client};

    while (!stop_flag.load() && client.is_open())
    {
        const auto result = client.receive();
        if (!result)
            break;
        if (result.value.empty())
            continue;

        const auto parse_result = parseHeader(result.value);
        if (!parse_result)
            break;

        const Header header = parse_result.value;

        if (const size_t expected = Header::SIZE + header.length; result.value.size() != expected)
            break;

        const std::span<const uint8_t> body(
            result.value.data() + Header::SIZE,
            header.length);

        dispatch(header, body, ctx, conn);
    }

    client.close();

    if (ctx.member_id != 0)
    {
        unregister_connection(ctx.member_id);
        handle_leave(ctx);
    }
}

void server::dispatch(const Header header, const std::span<const uint8_t> payload, clientContext& ctx,
                      clientConnection& conn)
{
    switch (header.opcode)
    {
    case Opcode::CREATE:
        {
            const result<std::string> parse_result = parseCreateMessage(payload);
            if (!parse_result)
            {
                sendError(conn, errcode::CREATE_FAILED, parse_result.message);
                break;
            }

            const std::string name = parse_result.value;
            const uint32_t session_id = next_session_id.fetch_add(1);

            if (const auto result = handle_create(session_id, name, ctx); !result)
            {
                sendError(conn, errcode::CREATE_FAILED, result.message);
                break;
            }

            register_connection(ctx.member_id, &conn);
            {
                const auto ack_payload = serializeCreateAckMessage({
                    .ack_code = ackcode::CREATE_OK,
                    .session_id = ctx.session_id,
                    .member_id = ctx.member_id
                });
                const Message ack_msg{
                    .header = Header{
                        .opcode = Opcode::ACK, .flags = 0, .length = static_cast<uint32_t>(ack_payload.size())
                    },
                    .payload = ack_payload
                };
                conn.send(serializeMessage(ack_msg));
            }
            break;
        }
    case Opcode::JOIN:
        {
            result<JoinMessage> parse_result = parseJoinMessage(payload);
            if (!parse_result)
            {
                sendError(conn, errcode::JOIN_FAILED, parse_result.message);
                break;
            }

            const auto [session_id, name] = parse_result.value;
            if (const auto result = handle_join(session_id, name, ctx); !result)
            {
                sendError(conn, errcode::JOIN_FAILED, result.message);
                break;
            }

            register_connection(ctx.member_id, &conn);
            {
                const auto ack_payload = serializeJoinAckMessage({
                    .ack_code = ackcode::JOIN_OK,
                    .member_id = ctx.member_id
                });
                const Message ack_msg{
                    .header = Header{
                        .opcode = Opcode::ACK, .flags = 0, .length = static_cast<uint32_t>(ack_payload.size())
                    },
                    .payload = ack_payload
                };
                conn.send(serializeMessage(ack_msg));
            }
            break;
        }
    case Opcode::LEAVE:
        {
            if (const auto result = handle_leave(ctx); !result)
                sendError(conn, errcode::LEAVE_FAILED, result.message);
            else
                sendAck(conn, ackcode::LEFT, "left session");
            break;
        }
    case Opcode::CLOSE_SESSION:
        {
            if (const auto result = handle_close_session(ctx); !result)
                sendError(conn, errcode::LEAVE_FAILED, result.message);
            else
                sendAck(conn, ackcode::OK, "session closed");
            break;
        }
    case Opcode::CANVAS_STATE_REQUEST:
        {
            handle_canvas_state_request(ctx, conn);
            break;
        }
    case Opcode::CANVAS_STATE:
        {
            handle_canvas_state(payload, ctx, conn);
            break;
        }
    case Opcode::DRAW:
        {
            handle_draw(payload, ctx, conn);
            break;
        }
    default:
        sendError(conn, errcode::UNKNOWN, "unknown opcode");
        break;
    }
}

result<bool> server::handle_create(const uint32_t session_id, const std::string& name, clientContext& client_context)
{
    const uint32_t member_id = next_member_id.fetch_add(1);

    std::lock_guard lock(sessions_mutex);
    auto [it, inserted] = sessions.try_emplace(session_id);
    if (!inserted)
        return {.value = false, .err = error::none, .message = "session ID already exists"};

    session& new_session = it->second;

    new_session.session_id = session_id;
    new_session.host_id = member_id;
    new_session.state = session_state::open;
    new_session.created_at = std::chrono::steady_clock::now();

    member host_member;
    host_member.id = member_id;
    host_member.name = name;
    host_member.role = member_role::host;
    host_member.session_id = session_id;

    {
        std::lock_guard members_lock(new_session.members_mutex);
        new_session.members.emplace(host_member.id, host_member);
    }

    client_context.member_id = member_id;
    client_context.session_id = session_id;

    return {.value = true, .err = error::none, .message = {}};
}

result<bool> server::handle_join(const uint32_t session_id, const std::string& name, clientContext& ctx)
{
    const uint32_t member_id = next_member_id.fetch_add(1);
    std::lock_guard lock(sessions_mutex);

    const auto it = sessions.find(session_id);
    if (it == sessions.end())
        return {.value = false, .err = error::none, .message = "no such session"};

    session& sess = it->second;

    if (sess.state != session_state::open)
        return {.value = false, .err = error::none, .message = "session is not open"};

    member new_member;
    new_member.id = member_id;
    new_member.name = name;
    new_member.role = member_role::participant;
    new_member.session_id = session_id;

    std::lock_guard members_lock(sess.members_mutex);
    sess.members.emplace(member_id, new_member);

    ctx.member_id = member_id;
    ctx.session_id = session_id;

    const auto payload = serializeMemberJoinedNotification({
        .notif_code = notifcode::MEMBER_JOINED,
        .member_id = member_id, .name = name
    });
    sendNotification(sess, Opcode::NOTIFICATION, payload, member_id);

    return {.value = true, .err = error::none, .message = {}};
}

result<bool> server::handle_leave(clientContext& ctx)
{
    if (ctx.member_id == 0)
        return {.value = false, .err = error::none, .message = "not in a session"};

    std::lock_guard lock(sessions_mutex);

    const auto it = sessions.find(ctx.session_id);
    if (it == sessions.end())
        return {.value = false, .err = error::none, .message = "session not found"};

    session& sess = it->second;
    const bool was_host = sess.is_host(ctx.member_id);

    std::string leaving_name;
    bool session_empty = false;
    {
        std::lock_guard members_lock(sess.members_mutex);
        if (const auto mit = sess.members.find(ctx.member_id); mit != sess.members.end())
            leaving_name = mit->second.name;
        sess.members.erase(ctx.member_id);
        session_empty = sess.members.empty();
    }

    const uint32_t leaving_id = ctx.member_id;
    ctx.member_id = 0;
    ctx.session_id = 0;

    if (was_host || session_empty)
    {
        std::vector<uint32_t> ids;
        {
            std::lock_guard members_lock(sess.members_mutex);
            ids.reserve(sess.members.size());
            for (const auto id : sess.members | std::views::keys)
                ids.push_back(id);
        }
        const auto payload = serializeSessionClosedNotification({.notif_code = notifcode::SESSION_CLOSED});
        const Message frame{
            .header = Header{
                .opcode = Opcode::NOTIFICATION, .flags = 0, .length = static_cast<uint32_t>(payload.size())
            },
            .payload = payload
        };
        const auto buf = serializeMessage(frame);
        for (const auto id : ids)
            if (auto* conn = find_connection(id))
                conn->send(buf);
        sessions.erase(it);
    }
    else
    {
        const auto payload = serializeMemberLeftNotification({
            .notif_code = notifcode::MEMBER_LEFT,
            .member_id = leaving_id, .name = leaving_name
        });
        sendNotification(sess, Opcode::NOTIFICATION, payload);
    }

    return {.value = true, .err = error::none, .message = {}};
}

result<bool> server::handle_close_session(clientContext& ctx)
{
    if (ctx.member_id == 0)
        return {.value = false, .err = error::none, .message = "not in a session"};

    {
        std::lock_guard lock(sessions_mutex);
        const auto it = sessions.find(ctx.session_id);
        if (it == sessions.end())
            return {.value = false, .err = error::none, .message = "session not found"};

        if (!it->second.is_host(ctx.member_id))
            return {.value = false, .err = error::none, .message = "only the host can close the session"};
    }

    return handle_leave(ctx);
}

void server::handle_canvas_state_request(const clientContext& ctx, clientConnection& conn)
{
    if (ctx.member_id == 0 || ctx.session_id == 0)
    {
        sendError(conn, errcode::UNKNOWN, "not in a session");
        return;
    }

    std::lock_guard lock(sessions_mutex);
    const auto it = sessions.find(ctx.session_id);
    if (it == sessions.end())
    {
        sendError(conn, errcode::UNKNOWN, "session not found");
        return;
    }

    const session& sess = it->second;
    if (auto* host_conn = find_connection(sess.host_id))
    {
        const Message request{
            .header = Header{
                .opcode = Opcode::CANVAS_STATE_REQUEST,
                .flags = 0,
                .length = 0
            },
            .payload = {}
        };
        host_conn->send(serializeMessage(request));
        return;
    }

    sendError(conn, errcode::UNKNOWN, "host is not connected");
}

void server::handle_canvas_state(std::span<const uint8_t> payload, const clientContext& ctx, clientConnection& conn)
{
    if (ctx.member_id == 0 || ctx.session_id == 0)
    {
        sendError(conn, errcode::UNKNOWN, "not in a session");
        return;
    }

    std::lock_guard lock(sessions_mutex);
    const auto it = sessions.find(ctx.session_id);
    if (it == sessions.end())
    {
        sendError(conn, errcode::UNKNOWN, "session not found");
        return;
    }

    const session& sess = it->second;
    if (!sess.is_host(ctx.member_id))
    {
        sendError(conn, errcode::UNKNOWN, "only the host can send canvas state");
        return;
    }

    if (const auto state_res = parseCanvasStateMessage(payload); !state_res)
    {
        sendError(conn, errcode::UNKNOWN, state_res.message);
        return;
    }

    const std::vector state_payload(payload.begin(), payload.end());
    sendNotification(sess, Opcode::CANVAS_STATE, state_payload, ctx.member_id);
}

void server::sendNotification(const session& sess, const uint8_t opcode, const std::vector<uint8_t>& payload,
                              const uint32_t exclude_id)
{
    std::vector<uint32_t> ids;
    {
        std::lock_guard lock(sess.members_mutex);
        for (const auto id : sess.members | std::views::keys)
            if (id != exclude_id)
                ids.push_back(id);
    }
    const Message frame{
        .header = Header{.opcode = opcode, .flags = 0, .length = static_cast<uint32_t>(payload.size())},
        .payload = payload
    };
    const auto buf = serializeMessage(frame);
    for (const auto id : ids)
        if (auto* conn = find_connection(id))
            conn->send(buf);
}

void server::sendAck(clientConnection& conn, const uint8_t ack_code, const std::string& message_text)
{
    const AckMessage ack{.ack_code = ack_code, .message = message_text};
    const auto payload = serializeAckMessage(ack);
    const Message frame{
        .header = Header{.opcode = Opcode::ACK, .flags = 0, .length = static_cast<uint32_t>(payload.size())},
        .payload = payload
    };

    conn.send(serializeMessage(frame));
}

void server::sendError(clientConnection& conn, const uint8_t err_code, const std::string& message_text)
{
    const ErrorMessage error_message{.err_code = err_code, .err_message = message_text};
    const Message message{
        .header = Header{
            .opcode = Opcode::ERROR_MSG, .flags = 0,
            .length = static_cast<uint32_t>(serializeErrorMessage(error_message).size())
        },
        .payload = serializeErrorMessage(error_message)
    };

    const auto buffer = serializeMessage(message);
    conn.send(buffer);
}

session* server::find_session(const uint32_t session_id)
{
    std::lock_guard lock(sessions_mutex);
    const auto it = sessions.find(session_id);
    return it != sessions.end() ? &it->second : nullptr;
}

void server::register_connection(const uint32_t member_id, clientConnection* conn)
{
    std::lock_guard lock(connections_mutex);
    connections[member_id] = conn;
}

void server::unregister_connection(const uint32_t member_id)
{
    std::lock_guard lock(connections_mutex);
    connections.erase(member_id);
}

clientConnection* server::find_connection(const uint32_t member_id)
{
    std::lock_guard lock(connections_mutex);
    const auto it = connections.find(member_id);
    return it != connections.end() ? it->second : nullptr;
}

void server::handle_draw(const std::span<const uint8_t> payload, const clientContext& ctx, clientConnection& conn)
{
    if (ctx.member_id == 0 || ctx.session_id == 0)
    {
        sendError(conn, errcode::UNKNOWN, "not in a session");
        return;
    }

    auto op_res = parseDrawOperation(payload);
    if (!op_res)
    {
        sendError(conn, errcode::UNKNOWN, op_res.message);
        return;
    }

    draw_operation op = std::move(op_res.value);

    std::lock_guard lock(sessions_mutex);
    const auto it = sessions.find(ctx.session_id);
    if (it == sessions.end())
    {
        sendError(conn, errcode::UNKNOWN, "session not found");
        return;
    }

    const session& sess = it->second;

    if (!sess.is_host(ctx.member_id))
    {
        op.member_id = ctx.member_id;
        op.seq = 0;
        if (const auto validation = validateDrawOperation(op, false); !validation)
        {
            sendError(conn, errcode::UNKNOWN, validation.message);
            return;
        }
        const auto host_conn = find_connection(sess.host_id);
        if (host_conn == nullptr)
        {
            sendError(conn, errcode::UNKNOWN, "host is not connected");
            return;
        }

        const auto forwarded_payload = serializeDrawOperation(op);
        const Message forwarded{
            .header = Header{
                .opcode = Opcode::DRAW,
                .flags = 0,
                .length = static_cast<uint32_t>(forwarded_payload.size())
            },
            .payload = forwarded_payload
        };
        host_conn->send(serializeMessage(forwarded));
        return;
    }

    if (const auto validation = validateDrawOperation(op, true); !validation)
    {
        sendError(conn, errcode::UNKNOWN, validation.message);
        return;
    }

    const auto draw_payload = serializeDrawOperation(op);
    sendNotification(sess, Opcode::DRAW, draw_payload, ctx.member_id);

}
