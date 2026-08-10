#include "common/protocol/message.h"
#include <cstring>
#include <winsock2.h>

result<Header> parseHeader(const std::span<const uint8_t> data)
{
    if (data.size() < Header::SIZE)
    {
        return {
            .value = {},
            .err = error::malformed,
            .message = "not enough data for header"
        };
    }

    Header header{};

    header.opcode = data[0];
    header.flags = data[1];

    uint32_t length;

    std::memcpy(
        &length,
        data.data() + 2,
        sizeof(length)
    );

    header.length = ntohl(length);

    return {
        .value = header,
        .err = error::none,
        .message = {}
    };
}

result<std::string> parseCreateMessage(const std::span<const uint8_t> data)
{
    if (data.empty())
        return {.value = {}, .err = error::malformed, .message = "not enough data"};

    const uint8_t length = data[0];

    if (data.size() < static_cast<size_t>(1) + length)
        return {.value = {}, .err = error::malformed, .message = "declared length exceeds available data"};

    std::string name(
        reinterpret_cast<const char*>(data.data() + 1),
        length
    );

    return {.value = std::move(name), .err = error::none, .message = {}};
}

result<JoinMessage> parseJoinMessage(const std::span<const uint8_t> data)
{
    if (data.size() < sizeof(uint32_t) + 1)
        return {.value = {}, .err = error::malformed, .message = "not enough data"};

    uint32_t session_id;
    std::memcpy(&session_id, data.data(), sizeof(session_id));
    session_id = ntohl(session_id);

    const uint8_t name_length = data[sizeof(uint32_t)];

    if (const size_t expected = sizeof(uint32_t) + 1 + name_length; data.size() < expected)
        return {.value = {}, .err = error::malformed, .message = "declared length exceeds available data"};

    std::string name(
        reinterpret_cast<const char*>(data.data() + sizeof(uint32_t) + 1),
        name_length
    );

    return {
        .value = JoinMessage{ .session_id = session_id, .name = std::move(name) },
        .err = error::none,
        .message = {}
    };
}

result<AckMessage> parseAckMessage(const std::span<const uint8_t> data)
{
    if (data.empty())
        return {.value = {}, .err = error::malformed, .message = "not enough data"};

    const uint8_t length = data[0];
    if (data.size() < static_cast<size_t>(1) + length)
        return {.value = {}, .err = error::malformed, .message = "declared length exceeds available data"};

    std::string message(
        reinterpret_cast<const char*>(data.data() + 1),
        length
    );

    return {.value = AckMessage{ .message = std::move(message) }, .err = error::none, .message = {}};
}

std::vector<uint8_t> serializeMessage(const Message& message)
{
    std::vector<uint8_t> buffer(message.getSize());

    buffer[0] = message.header.opcode;
    buffer[1] = message.header.flags;

    const uint32_t length = htonl(
        static_cast<uint32_t>(message.payload.size())
    );

    std::memcpy(
        buffer.data() + 2,
        &length,
        sizeof(length)
    );

    if (!message.payload.empty())
    {
        std::memcpy(
            buffer.data() + Header::SIZE,
            message.payload.data(),
            message.payload.size()
        );
    }

    return buffer;
}

std::vector<uint8_t> serializeAckMessage(const AckMessage &message)
{
    std::vector<uint8_t> buffer(1 + message.message.size());
    buffer[0] = static_cast<uint8_t>(message.message.size());
    if (!message.message.empty())
    {
        std::memcpy(
            buffer.data() + 1,
            message.message.data(),
            message.message.size()
        );
    }
    return buffer;
}

result<ErrorMessage> parseErrorMessage(const std::span<const uint8_t> data)
{
    if (data.empty())
        return {.value = {}, .err = error::malformed, .message = "not enough data"};

    const uint8_t err_code = data[0];
    std::string err_message(
        reinterpret_cast<const char*>(data.data() + 1),
        data.size() - 1
    );

    return {
        .value = ErrorMessage{ .err_code = err_code, .err_message = std::move(err_message) },
        .err = error::none,
        .message = {}
    };
}

std::vector<uint8_t> serializeErrorMessage(const ErrorMessage &message)
{
    std::vector<uint8_t> buffer(1 + message.err_message.size());
    buffer[0] = message.err_code;
    std::memcpy(
        buffer.data() + 1,
        message.err_message.data(),
        message.err_message.size()
        );
    return buffer;
}

std::vector<uint8_t> serializeMemberJoinedNotification(const MemberJoinedNotification& notif)
{
    std::vector<uint8_t> buf(sizeof(uint32_t) + 1 + notif.name.size());
    const uint32_t id = htonl(notif.member_id);
    std::memcpy(buf.data(), &id, sizeof(id));
    buf[sizeof(uint32_t)] = static_cast<uint8_t>(notif.name.size());
    std::memcpy(buf.data() + sizeof(uint32_t) + 1, notif.name.data(), notif.name.size());
    return buf;
}

std::vector<uint8_t> serializeMemberLeftNotification(const MemberLeftNotification& notif)
{
    std::vector<uint8_t> buf(sizeof(uint32_t) + 1 + notif.name.size());
    const uint32_t id = htonl(notif.member_id);
    std::memcpy(buf.data(), &id, sizeof(id));
    buf[sizeof(uint32_t)] = static_cast<uint8_t>(notif.name.size());
    std::memcpy(buf.data() + sizeof(uint32_t) + 1, notif.name.data(), notif.name.size());
    return buf;
}

std::vector<uint8_t> serializeSessionClosedNotification(const SessionClosedNotification&)
{
    return {};
}

std::vector<uint8_t> serializeCreateMessage(const CreateMessage& msg)
{
    std::vector<uint8_t> buf(1 + msg.name.size());
    buf[0] = static_cast<uint8_t>(msg.name.size());
    std::memcpy(buf.data() + 1, msg.name.data(), msg.name.size());
    return buf;
}

std::vector<uint8_t> serializeJoinMessage(const JoinMessage& msg)
{
    std::vector<uint8_t> buf(sizeof(uint32_t) + 1 + msg.name.size());
    const uint32_t sid = htonl(msg.session_id);
    std::memcpy(buf.data(), &sid, sizeof(sid));
    buf[sizeof(uint32_t)] = static_cast<uint8_t>(msg.name.size());
    std::memcpy(buf.data() + sizeof(uint32_t) + 1, msg.name.data(), msg.name.size());
    return buf;
}

std::vector<uint8_t> serializeCreateAckMessage(const CreateAckMessage& msg)
{
    std::vector<uint8_t> buf(sizeof(uint32_t) * 2);
    const uint32_t sid = htonl(msg.session_id);
    const uint32_t mid = htonl(msg.member_id);
    std::memcpy(buf.data(), &sid, sizeof(sid));
    std::memcpy(buf.data() + sizeof(uint32_t), &mid, sizeof(mid));
    return buf;
}

result<CreateAckMessage> parseCreateAckMessage(const std::span<const uint8_t> data)
{
    if (data.size() < sizeof(uint32_t) * 2)
        return {.value = {}, .err = error::malformed, .message = "not enough data"};

    uint32_t sid, mid;
    std::memcpy(&sid, data.data(), sizeof(sid));
    std::memcpy(&mid, data.data() + sizeof(uint32_t), sizeof(mid));

    return {
        .value = CreateAckMessage{.session_id = ntohl(sid), .member_id = ntohl(mid)},
        .err = error::none
    };
}

std::vector<uint8_t> serializeJoinAckMessage(const JoinAckMessage& msg)
{
    std::vector<uint8_t> buf(sizeof(uint32_t));
    const uint32_t mid = htonl(msg.member_id);
    std::memcpy(buf.data(), &mid, sizeof(mid));
    return buf;
}

result<JoinAckMessage> parseJoinAckMessage(const std::span<const uint8_t> data)
{
    if (data.size() < sizeof(uint32_t))
        return {.value = {}, .err = error::malformed, .message = "not enough data"};

    uint32_t mid;
    std::memcpy(&mid, data.data(), sizeof(mid));

    return {.value = JoinAckMessage{.member_id = ntohl(mid)}, .err = error::none};
}

result<MemberJoinedNotification> parseMemberJoinedNotification(const std::span<const uint8_t> data)
{
    if (data.size() < sizeof(uint32_t) + 1)
        return {.value = {}, .err = error::malformed, .message = "not enough data"};

    uint32_t id;
    std::memcpy(&id, data.data(), sizeof(id));
    id = ntohl(id);

    const uint8_t name_len = data[sizeof(uint32_t)];
    if (data.size() < sizeof(uint32_t) + 1 + name_len)
        return {.value = {}, .err = error::malformed, .message = "name truncated"};

    std::string name(reinterpret_cast<const char*>(data.data() + sizeof(uint32_t) + 1), name_len);
    return {.value = MemberJoinedNotification{.member_id = id, .name = std::move(name)}, .err = error::none};
}

result<MemberLeftNotification> parseMemberLeftNotification(const std::span<const uint8_t> data)
{
    if (data.size() < sizeof(uint32_t) + 1)
        return {.value = {}, .err = error::malformed, .message = "not enough data"};

    uint32_t id;
    std::memcpy(&id, data.data(), sizeof(id));
    id = ntohl(id);

    const uint8_t name_len = data[sizeof(uint32_t)];
    if (data.size() < sizeof(uint32_t) + 1 + name_len)
        return {.value = {}, .err = error::malformed, .message = "name truncated"};

    std::string name(reinterpret_cast<const char*>(data.data() + sizeof(uint32_t) + 1), name_len);
    return {.value = MemberLeftNotification{.member_id = id, .name = std::move(name)}, .err = error::none};
}

std::vector<uint8_t> serializeCanvasStateMessage(const CanvasStateMessage& msg)
{
    std::vector<std::vector<uint8_t>> serialized;
    serialized.reserve(msg.operations.size());
    size_t total = 4; // op_count
    for (const auto& op : msg.operations)
    {
        serialized.push_back(serializeDrawOperation(op));
        total += 4 + serialized.back().size(); // length prefix + data
    }

    std::vector<uint8_t> buf;
    buf.reserve(total);
    buf.resize(4);

    const uint32_t count = htonl(static_cast<uint32_t>(msg.operations.size()));
    std::memcpy(buf.data(), &count, 4);

    for (const auto& s : serialized)
    {
        const uint32_t len = htonl(static_cast<uint32_t>(s.size()));
        const size_t pos = buf.size();
        buf.resize(pos + 4 + s.size());
        std::memcpy(buf.data() + pos, &len, 4);
        std::memcpy(buf.data() + pos + 4, s.data(), s.size());
    }

    return buf;
}

result<CanvasStateMessage> parseCanvasStateMessage(const std::span<const uint8_t> data)
{
    if (data.size() < 4)
        return {.value = {}, .err = error::malformed, .message = "canvas state too short"};

    uint32_t count;
    std::memcpy(&count, data.data(), 4);
    count = ntohl(count);

    CanvasStateMessage msg;
    msg.operations.reserve(count);

    size_t off = 4;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (off + 4 > data.size())
            return {.value = {}, .err = error::malformed, .message = "canvas state truncated"};

        uint32_t len;
        std::memcpy(&len, data.data() + off, 4);
        len = ntohl(len);
        off += 4;

        if (off + len > data.size())
            return {.value = {}, .err = error::malformed, .message = "operation data truncated"};

        auto op_res = parseDrawOperation(std::span<const uint8_t>(data.data() + off, len));
        if (!op_res)
            return {.value = {}, .err = op_res.err, .message = op_res.message};

        msg.operations.push_back(std::move(op_res.value));
        off += len;
    }

    return {.value = std::move(msg), .err = error::none};
}

