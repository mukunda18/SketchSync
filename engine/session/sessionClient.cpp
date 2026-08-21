#include "engine/session/sessionClient.h"

sessionClient::sessionClient(tcpSocket& tcp)
    : conn_{.tcp = &tcp}
{
}

sessionClient::sessionClient(webSocket& ws)
    : conn_{.ws = &ws}
{
}

result<Message> sessionClient::receive_msg() const
{
    if (conn_.tcp)
    {
        const auto hdr_res = conn_.tcp->receive(Header::SIZE);
        if (!hdr_res)
            return {.value = {}, .err = hdr_res.err, .message = hdr_res.message};

        const auto parse = parseHeader(hdr_res.value);
        if (!parse)
            return {.value = {}, .err = parse.err, .message = parse.message};

        const Header hdr = parse.value;
        std::vector<uint8_t> payload;

        if (hdr.length > 0)
        {
            auto pl = conn_.tcp->receive(hdr.length);
            if (!pl)
                return {.value = {}, .err = pl.err, .message = pl.message};
            payload = std::move(pl.value);
        }

        return {.value = Message{.header = hdr, .payload = std::move(payload)}, .err = error::none};
    }

    if (conn_.ws)
    {
        const auto res = conn_.ws->receive();
        if (!res)
            return {.value = {}, .err = res.err, .message = res.message};

        const auto parse = parseHeader(res.value);
        if (!parse)
            return {.value = {}, .err = parse.err, .message = parse.message};

        const Header hdr = parse.value;
        std::vector<uint8_t> payload;
        if (res.value.size() > Header::SIZE)
            payload.assign(res.value.begin() + Header::SIZE, res.value.end());

        return {.value = Message{.header = hdr, .payload = std::move(payload)}, .err = error::none};
    }

    return {.value = {}, .err = error::closed, .message = "no active connection"};
}

result<size_t> sessionClient::send_message(const Message& msg)
{
    return conn_.send(serializeMessage(msg));
}

result<bool> sessionClient::send_create(const std::string& name)
{
    const auto payload = serializeCreateMessage({.name = name});
    const Message msg{
        .header = Header{.opcode = Opcode::CREATE, .flags = 0, .length = static_cast<uint32_t>(payload.size())},
        .payload = payload
    };

    if (const auto r = send_message(msg); !r)
        return {.value = false, .err = r.err, .message = r.message};

    return {.value = true, .err = error::none};
}

result<bool> sessionClient::send_join(const uint32_t session_id, const std::string& name)
{
    const auto payload = serializeJoinMessage({.session_id = session_id, .name = name});
    const Message msg{
        .header = Header{.opcode = Opcode::JOIN, .flags = 0, .length = static_cast<uint32_t>(payload.size())},
        .payload = payload
    };

    if (const auto r = send_message(msg); !r)
        return {.value = false, .err = r.err, .message = r.message};

    return {.value = true, .err = error::none};
}

result<bool> sessionClient::send_leave()
{
    const Message msg{
        .header = Header{.opcode = Opcode::LEAVE, .flags = 0, .length = 0},
        .payload = {}
    };

    if (const auto r = send_message(msg); !r)
        return {.value = false, .err = r.err, .message = r.message};

    return {.value = true, .err = error::none};
}

result<bool> sessionClient::send_close_session()
{
    const Message msg{
        .header = Header{.opcode = Opcode::CLOSE_SESSION, .flags = 0, .length = 0},
        .payload = {}
    };

    if (const auto r = send_message(msg); !r)
        return {.value = false, .err = r.err, .message = r.message};

    return {.value = true, .err = error::none};
}

result<Message> sessionClient::poll() const
{
    return receive_msg();
}

result<bool> sessionClient::request_canvas_state()
{
    if (!in_session())
        return {.value = false, .err = error::rejected, .message = "not in a session"};

    const Message msg{
        .header = Header{.opcode = Opcode::CANVAS_STATE_REQUEST, .flags = 0, .length = 0},
        .payload = {}
    };
    if (const auto r = send_message(msg); !r)
        return {.value = false, .err = r.err, .message = r.message};
    return {.value = true, .err = ::error::none};
}

result<bool> sessionClient::send_draw(draw_operation op)
{
    op.member_id = member_id_;
    const auto payload = serializeDrawOperation(op);
    const Message msg{
        .header = Header{.opcode = Opcode::DRAW, .flags = 0, .length = static_cast<uint32_t>(payload.size())},
        .payload = payload
    };
    if (const auto r = send_message(msg); !r)
        return {.value = false, .err = r.err, .message = r.message};
    return {.value = true, .err = error::none};
}

result<bool> sessionClient::send_draw_raw(const draw_operation& op)
{
    const auto payload = serializeDrawOperation(op);
    const Message msg{
        .header = Header{.opcode = Opcode::DRAW, .flags = 0, .length = static_cast<uint32_t>(payload.size())},
        .payload = payload
    };

    if (const auto r = send_message(msg); !r)
        return {.value = false, .err = r.err, .message = r.message};

    return {.value = true, .err = error::none};
}

result<bool> sessionClient::send_canvas_state(const std::vector<draw_operation>& operations)
{
    if (!host_)
        return {.value = false, .err = ::error::rejected, .message = "only the host can send canvas state"};

    const CanvasStateMessage state{.operations = operations};
    const auto payload = serializeCanvasStateMessage(state);
    const Message msg{
        .header = Header{.opcode = Opcode::CANVAS_STATE, .flags = 0, .length = static_cast<uint32_t>(payload.size())},
        .payload = payload
    };

    if (const auto r = send_message(msg); !r)
        return {.value = false, .err = r.err, .message = r.message};

    return {.value = true, .err = error::none};
}
