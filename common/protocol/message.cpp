#include "common/protocol/message.h"
#include "common/bytes.h"

result<Header> parseHeader(const std::span<const uint8_t> data)
{
    if (data.size() < Header::SIZE)
        return {.value = {}, .err = error::malformed, .message = "not enough data for header"};

    size_t off = 0;
    Header header{};
    header.opcode = bytes::read8(data, off);
    header.flags  = bytes::read8(data, off);
    header.length = bytes::read32(data, off);

    return {.value = header, .err = error::none, .message = {}};
}

result<std::string> parseCreateMessage(const std::span<const uint8_t> data)
{
    if (data.empty())
        return {.value = {}, .err = error::malformed, .message = "not enough data"};

    size_t off = 0;
    const uint8_t length = bytes::read8(data, off);

    if (data.size() < 1 + length)
        return {.value = {}, .err = error::malformed, .message = "declared length exceeds available data"};

    std::string name(reinterpret_cast<const char*>(data.data() + off), length);

    return {.value = std::move(name), .err = error::none, .message = {}};
}

result<JoinMessage> parseJoinMessage(const std::span<const uint8_t> data)
{
    if (data.size() < sizeof(uint32_t) + 1)
        return {.value = {}, .err = error::malformed, .message = "not enough data"};

    size_t off = 0;
    const uint32_t session_id  = bytes::read32(data, off);
    const uint8_t  name_length = bytes::read8(data, off);

    if (data.size() < sizeof(uint32_t) + 1 + name_length)
        return {.value = {}, .err = error::malformed, .message = "declared length exceeds available data"};

    std::string name(reinterpret_cast<const char*>(data.data() + off), name_length);

    return {
        .value = JoinMessage{ .session_id = session_id, .name = std::move(name) },
        .err = error::none, .message = {}
    };
}

result<AckMessage> parseAckMessage(const std::span<const uint8_t> data)
{
    if (data.empty())
        return {.value = {}, .err = error::malformed, .message = "not enough data"};

    size_t off = 0;
    const uint8_t ack_code = bytes::read8(data, off);
    std::string message(reinterpret_cast<const char*>(data.data() + off), data.size() - off);

    return {.value = AckMessage{ .ack_code = ack_code, .message = std::move(message) }, .err = error::none, .message = {}};
}

std::vector<uint8_t> serializeMessage(const Message& message)
{
    std::vector<uint8_t> buffer(message.getSize());
    size_t off = 0;

    bytes::write8(buffer, off, message.header.opcode);
    bytes::write8(buffer, off, message.header.flags);
    bytes::write32(buffer, off, message.header.length);

    if (!message.payload.empty())
        std::memcpy(buffer.data() + off, message.payload.data(), message.payload.size());

    return buffer;
}

std::vector<uint8_t> serializeAckMessage(const AckMessage& message)
{
    std::vector<uint8_t> buffer(1 + message.message.size());
    size_t off = 0;
    bytes::write8(buffer, off, message.ack_code);
    if (!message.message.empty())
        std::memcpy(buffer.data() + off, message.message.data(), message.message.size());
    return buffer;
}

result<ErrorMessage> parseErrorMessage(const std::span<const uint8_t> data)
{
    if (data.empty())
        return {.value = {}, .err = error::malformed, .message = "not enough data"};

    size_t off = 0;
    const uint8_t err_code = bytes::read8(data, off);
    std::string err_message(reinterpret_cast<const char*>(data.data() + off), data.size() - off);

    return {
        .value = ErrorMessage{ .err_code = err_code, .err_message = std::move(err_message) },
        .err = error::none, .message = {}
    };
}

std::vector<uint8_t> serializeErrorMessage(const ErrorMessage& message)
{
    std::vector<uint8_t> buffer(1 + message.err_message.size());
    size_t off = 0;
    bytes::write8(buffer, off, message.err_code);
    std::memcpy(buffer.data() + off, message.err_message.data(), message.err_message.size());
    return buffer;
}

std::vector<uint8_t> serializeMemberJoinedNotification(const MemberJoinedNotification& notif)
{
    std::vector<uint8_t> buf(1 + sizeof(uint32_t) + 1 + notif.name.size());
    size_t off = 0;
    bytes::write8(buf, off, notif.notif_code);
    bytes::write32(buf, off, notif.member_id);
    bytes::write8(buf, off, static_cast<uint8_t>(notif.name.size()));
    std::memcpy(buf.data() + off, notif.name.data(), notif.name.size());
    return buf;
}

std::vector<uint8_t> serializeMemberLeftNotification(const MemberLeftNotification& notif)
{
    std::vector<uint8_t> buf(1 + sizeof(uint32_t) + 1 + notif.name.size());
    size_t off = 0;
    bytes::write8(buf, off, notif.notif_code);
    bytes::write32(buf, off, notif.member_id);
    bytes::write8(buf, off, static_cast<uint8_t>(notif.name.size()));
    std::memcpy(buf.data() + off, notif.name.data(), notif.name.size());
    return buf;
}

std::vector<uint8_t> serializeSessionClosedNotification(const SessionClosedNotification& notif)
{
    std::vector<uint8_t> buf(1);
    size_t off = 0;
    bytes::write8(buf, off, notif.notif_code);
    return buf;
}

result<SessionClosedNotification> parseSessionClosedNotification(const std::span<const uint8_t> data)
{
    if (data.empty())
        return {.value = {}, .err = error::malformed, .message = "not enough data"};

    size_t off = 0;
    return {.value = SessionClosedNotification{.notif_code = bytes::read8(data, off)}, .err = error::none};
}

std::vector<uint8_t> serializeCreateMessage(const CreateMessage& msg)
{
    std::vector<uint8_t> buf(1 + msg.name.size());
    size_t off = 0;
    bytes::write8(buf, off, static_cast<uint8_t>(msg.name.size()));
    std::memcpy(buf.data() + off, msg.name.data(), msg.name.size());
    return buf;
}

std::vector<uint8_t> serializeJoinMessage(const JoinMessage& msg)
{
    std::vector<uint8_t> buf(sizeof(uint32_t) + 1 + msg.name.size());
    size_t off = 0;
    bytes::write32(buf, off, msg.session_id);
    bytes::write8(buf, off, static_cast<uint8_t>(msg.name.size()));
    std::memcpy(buf.data() + off, msg.name.data(), msg.name.size());
    return buf;
}

std::vector<uint8_t> serializeCreateAckMessage(const CreateAckMessage& msg)
{
    std::vector<uint8_t> buf(1 + sizeof(uint32_t) * 2);
    size_t off = 0;
    bytes::write8(buf, off, msg.ack_code);
    bytes::write32(buf, off, msg.session_id);
    bytes::write32(buf, off, msg.member_id);
    return buf;
}

result<CreateAckMessage> parseCreateAckMessage(const std::span<const uint8_t> data)
{
    if (data.size() < 1 + sizeof(uint32_t) * 2)
        return {.value = {}, .err = error::malformed, .message = "not enough data"};

    size_t off = 0;
    const uint8_t  ac  = bytes::read8(data, off);
    const uint32_t sid = bytes::read32(data, off);
    const uint32_t mid = bytes::read32(data, off);

    return {.value = CreateAckMessage{.ack_code = ac, .session_id = sid, .member_id = mid}, .err = error::none};
}

std::vector<uint8_t> serializeJoinAckMessage(const JoinAckMessage& msg)
{
    std::vector<uint8_t> buf(1 + sizeof(uint32_t));
    size_t off = 0;
    bytes::write8(buf, off, msg.ack_code);
    bytes::write32(buf, off, msg.member_id);
    return buf;
}

result<JoinAckMessage> parseJoinAckMessage(const std::span<const uint8_t> data)
{
    if (data.size() < 1 + sizeof(uint32_t))
        return {.value = {}, .err = error::malformed, .message = "not enough data"};

    size_t off = 0;
    const uint8_t ac = bytes::read8(data, off);
    return {.value = JoinAckMessage{.ack_code = ac, .member_id = bytes::read32(data, off)}, .err = error::none};
}

result<MemberJoinedNotification> parseMemberJoinedNotification(const std::span<const uint8_t> data)
{
    if (data.size() < 1 + sizeof(uint32_t) + 1)
        return {.value = {}, .err = error::malformed, .message = "not enough data"};

    size_t off = 0;
    const uint8_t  notif_code = bytes::read8(data, off);
    const uint32_t id         = bytes::read32(data, off);
    const uint8_t  name_len   = bytes::read8(data, off);

    if (data.size() < off + name_len)
        return {.value = {}, .err = error::malformed, .message = "name truncated"};

    std::string name(reinterpret_cast<const char*>(data.data() + off), name_len);
    return {.value = MemberJoinedNotification{.notif_code = notif_code, .member_id = id, .name = std::move(name)}, .err = error::none};
}

result<MemberLeftNotification> parseMemberLeftNotification(const std::span<const uint8_t> data)
{
    if (data.size() < 1 + sizeof(uint32_t) + 1)
        return {.value = {}, .err = error::malformed, .message = "not enough data"};

    size_t off = 0;
    const uint8_t  notif_code = bytes::read8(data, off);
    const uint32_t id         = bytes::read32(data, off);
    const uint8_t  name_len   = bytes::read8(data, off);

    if (data.size() < off + name_len)
        return {.value = {}, .err = error::malformed, .message = "name truncated"};

    std::string name(reinterpret_cast<const char*>(data.data() + off), name_len);
    return {.value = MemberLeftNotification{.notif_code = notif_code, .member_id = id, .name = std::move(name)}, .err = error::none};
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
    size_t hdr_off = 0;
    bytes::write32(buf, hdr_off, static_cast<uint32_t>(msg.operations.size()));

    for (const auto& s : serialized)
    {
        const size_t pos = buf.size();
        buf.resize(pos + 4 + s.size());
        size_t len_off = pos;
        bytes::write32(buf, len_off, static_cast<uint32_t>(s.size()));
        std::memcpy(buf.data() + pos + 4, s.data(), s.size());
    }

    return buf;
}

result<CanvasStateMessage> parseCanvasStateMessage(const std::span<const uint8_t> data)
{
    if (data.size() < 4)
        return {.value = {}, .err = error::malformed, .message = "canvas state too short"};

    size_t off = 0;
    const uint32_t count = bytes::read32(data, off);

    CanvasStateMessage msg;
    msg.operations.reserve(count);

    for (uint32_t i = 0; i < count; ++i)
    {
        if (off + 4 > data.size())
            return {.value = {}, .err = error::malformed, .message = "canvas state truncated"};

        const uint32_t len = bytes::read32(data, off);

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
