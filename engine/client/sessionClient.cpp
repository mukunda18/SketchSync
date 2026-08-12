#include "engine/client/sessionClient.h"

sessionClient::sessionClient(tcpSocket& tcp)
    : conn_{.tcp = &tcp}
{
}

sessionClient::sessionClient(webSocket& ws)
    : conn_{.ws = &ws}
{
}

result<Message> sessionClient::receive_one() const
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
        std::vector<uint8_t> payload(res.value.begin() + Header::SIZE, res.value.end());

        return {.value = Message{.header = hdr, .payload = std::move(payload)}, .err = error::none};
    }

    return {.value = {}, .err = error::closed, .message = "no active connection"};
}

result<size_t> sessionClient::send_message(const Message& msg)
{
    return conn_.send(serializeMessage(msg));
}

result<uint32_t> sessionClient::create(const std::string& name)
{
    const auto payload = serializeCreateMessage({.name = name});
    const Message msg{
        .header = Header{.opcode = Opcode::CREATE, .flags = 0, .length = static_cast<uint32_t>(payload.size())},
        .payload = payload
    };

    if (const auto r = send_message(msg); !r)
        return {.value = 0, .err = r.err, .message = r.message};

    const auto res = receive_one();
    if (!res)
        return {.value = 0, .err = res.err, .message = res.message};

    if (res.value.header.opcode == Opcode::ERROR_MSG)
    {
        const auto err = parseErrorMessage(res.value.payload);
        return {.value = 0, .err = error::rejected, .message = err ? err.value.err_message : "server error"};
    }

    if (res.value.header.opcode != Opcode::ACK)
        return {.value = 0, .err = error::malformed, .message = "unexpected response opcode"};

    const auto ack = parseCreateAckMessage(res.value.payload);
    if (!ack)
        return {.value = 0, .err = ack.err, .message = ack.message};

    if (ack.value.ack_code != ackcode::CREATE_OK)
        return {.value = 0, .err = error::malformed, .message = "unexpected create ack code"};

    member_id_ = ack.value.member_id;
    session_id_ = ack.value.session_id;
    host_ = true;

    return {.value = session_id_, .err = error::none};
}

result<bool> sessionClient::join(const uint32_t session_id, const std::string& name)
{
    const auto payload = serializeJoinMessage({.session_id = session_id, .name = name});
    const Message msg{
        .header = Header{.opcode = Opcode::JOIN, .flags = 0, .length = static_cast<uint32_t>(payload.size())},
        .payload = payload
    };

    if (const auto r = send_message(msg); !r)
        return {.value = false, .err = r.err, .message = r.message};

    const auto res = receive_one();
    if (!res)
        return {.value = false, .err = res.err, .message = res.message};

    if (res.value.header.opcode == Opcode::ERROR_MSG)
    {
        const auto err = parseErrorMessage(res.value.payload);
        return {.value = false, .err = error::rejected, .message = err ? err.value.err_message : "server error"};
    }

    if (res.value.header.opcode != Opcode::ACK)
        return {.value = false, .err = error::malformed, .message = "unexpected response opcode"};

    const auto ack = parseJoinAckMessage(res.value.payload);
    if (!ack)
        return {.value = false, .err = ack.err, .message = ack.message};

    if (ack.value.ack_code != ackcode::JOIN_OK)
        return {.value = false, .err = error::malformed, .message = "unexpected join ack code"};

    member_id_ = ack.value.member_id;
    session_id_ = session_id;
    host_ = false;

    if (const auto state_request = request_canvas_state(); !state_request)
        return state_request;

    return {.value = true, .err = error::none};
}

result<bool> sessionClient::leave()
{
    const Message msg{
        .header = Header{.opcode = Opcode::LEAVE, .flags = 0, .length = 0},
        .payload = {}
    };

    if (const auto r = send_message(msg); !r)
        return {.value = false, .err = r.err, .message = r.message};

    const auto res = receive_one();
    if (!res)
        return {.value = false, .err = res.err, .message = res.message};

    if (res.value.header.opcode == Opcode::ERROR_MSG)
    {
        const auto err = parseErrorMessage(res.value.payload);
        return {.value = false, .err = error::rejected, .message = err ? err.value.err_message : "server error"};
    }

    if (res.value.header.opcode != Opcode::ACK)
        return {.value = false, .err = error::malformed, .message = "unexpected response opcode"};

    if (const auto ack = parseAckMessage(res.value.payload); !ack)
        return {.value = false, .err = ack.err, .message = ack.message};

    member_id_ = 0;
    session_id_ = 0;
    host_ = false;

    return {.value = true, .err = error::none};
}

result<bool> sessionClient::close_session()
{
    if (!host_)
        return {.value = false, .err = error::rejected, .message = "only the host can close the session"};

    const Message msg{
        .header = Header{.opcode = Opcode::CLOSE_SESSION, .flags = 0, .length = 0},
        .payload = {}
    };

    if (const auto r = send_message(msg); !r)
        return {.value = false, .err = r.err, .message = r.message};

    const auto res = receive_one();
    if (!res)
        return {.value = false, .err = res.err, .message = res.message};

    if (res.value.header.opcode == Opcode::ERROR_MSG)
    {
        const auto err = parseErrorMessage(res.value.payload);
        return {.value = false, .err = error::rejected, .message = err ? err.value.err_message : "server error"};
    }

    if (res.value.header.opcode != Opcode::ACK)
        return {.value = false, .err = error::malformed, .message = "unexpected response opcode"};

    const auto ack = parseAckMessage(res.value.payload);
    if (!ack)
        return {.value = false, .err = ack.err, .message = ack.message};

    if (ack.value.ack_code != ackcode::OK)
        return {.value = false, .err = error::malformed, .message = "unexpected close ack code"};

    member_id_ = 0;
    session_id_ = 0;
    host_ = false;

    return {.value = true, .err = error::none};
}

result<Message> sessionClient::poll() const
{
    return receive_one();
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
    return {.value = true, .err = error::none};
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
        return {.value = false, .err = error::rejected, .message = "only the host can send canvas state"};

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
