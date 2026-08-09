#include "engine/client/sessionClient.h"

sessionClient::sessionClient(tcpSocket& tcp)
    : conn_{ .tcp = &tcp }
{}

sessionClient::sessionClient(webSocket& ws)
    : conn_{ .ws = &ws }
{}

result<Message> sessionClient::receive_one()
{
    if (conn_.tcp)
    {
        auto hdr_res = conn_.tcp->receive(Header::SIZE);
        if (!hdr_res) return {.value = {}, .err = hdr_res.err, .message = hdr_res.message};

        auto parse = parseHeader(hdr_res.value);
        if (!parse) return {.value = {}, .err = parse.err, .message = parse.message};

        const Header hdr = parse.value;
        std::vector<uint8_t> payload;

        if (hdr.length > 0)
        {
            auto pl = conn_.tcp->receive(hdr.length);
            if (!pl) return {.value = {}, .err = pl.err, .message = pl.message};
            payload = std::move(pl.value);
        }

        return {.value = Message{.header = hdr, .payload = std::move(payload)}, .err = error::none};
    }

    if (conn_.ws)
    {
        auto res = conn_.ws->receive();
        if (!res) return {.value = {}, .err = res.err, .message = res.message};

        auto parse = parseHeader(res.value);
        if (!parse) return {.value = {}, .err = parse.err, .message = parse.message};

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
    if (!res) return {.value = 0, .err = res.err, .message = res.message};

    if (res.value.header.opcode == Opcode::ERROR_MSG)
    {
        const auto err = parseErrorMessage(res.value.payload);
        return {.value = 0, .err = error::rejected, .message = err ? err.value.err_message : "server error"};
    }

    if (res.value.header.opcode != Opcode::CREATE_ACK)
        return {.value = 0, .err = error::malformed, .message = "unexpected response opcode"};

    const auto ack = parseCreateAckMessage(res.value.payload);
    if (!ack) return {.value = 0, .err = ack.err, .message = ack.message};

    member_id_  = ack.value.member_id;
    session_id_ = ack.value.session_id;
    members_[member_id_] = remote_member{.id = member_id_, .name = name};

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
    if (!res) return {.value = false, .err = res.err, .message = res.message};

    if (res.value.header.opcode == Opcode::ERROR_MSG)
    {
        const auto err = parseErrorMessage(res.value.payload);
        return {.value = false, .err = error::rejected, .message = err ? err.value.err_message : "server error"};
    }

    if (res.value.header.opcode != Opcode::JOIN_ACK)
        return {.value = false, .err = error::malformed, .message = "unexpected response opcode"};

    const auto ack = parseJoinAckMessage(res.value.payload);
    if (!ack) return {.value = false, .err = ack.err, .message = ack.message};

    member_id_  = ack.value.member_id;
    session_id_ = session_id;
    members_[member_id_] = remote_member{.id = member_id_, .name = name};

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
    if (!res) return {.value = false, .err = res.err, .message = res.message};

    if (res.value.header.opcode == Opcode::ERROR_MSG)
    {
        const auto err = parseErrorMessage(res.value.payload);
        return {.value = false, .err = error::rejected, .message = err ? err.value.err_message : "server error"};
    }

    if (res.value.header.opcode != Opcode::ACK)
        return {.value = false, .err = error::malformed, .message = "unexpected response opcode"};

    member_id_  = 0;
    session_id_ = 0;
    members_.clear();

    return {.value = true, .err = error::none};
}

result<bool> sessionClient::poll()
{
    const auto res = receive_one();
    if (!res) return {.value = false, .err = res.err, .message = res.message};

    const auto& msg = res.value;

    switch (msg.header.opcode)
    {
    case Opcode::MEMBER_JOINED:
    {
        const auto notif = parseMemberJoinedNotification(msg.payload);
        if (!notif) break;
        members_[notif.value.member_id] = remote_member{.id = notif.value.member_id, .name = notif.value.name};
        if (on_member_joined)
            on_member_joined(notif.value.member_id, notif.value.name);
        break;
    }
    case Opcode::MEMBER_LEFT:
    {
        const auto notif = parseMemberLeftNotification(msg.payload);
        if (!notif) break;
        members_.erase(notif.value.member_id);
        if (on_member_left)
            on_member_left(notif.value.member_id, notif.value.name);
        break;
    }
    case Opcode::SESSION_CLOSED:
    {
        member_id_  = 0;
        session_id_ = 0;
        members_.clear();
        if (on_session_closed)
            on_session_closed();
        break;
    }
    default:
        return {.value = false, .err = error::malformed, .message = "unexpected opcode in poll"};
    }

    return {.value = true, .err = error::none};
}
