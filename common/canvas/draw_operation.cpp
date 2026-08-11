#include "common/canvas/draw_operation.h"
#include <cstring>
#include <winsock2.h>

std::vector<uint8_t> serializeDrawOperation(const draw_operation& op)
{
    const auto point_count = static_cast<uint16_t>(op.points.size());
    std::vector<uint8_t> buf(DRAW_OP_FIXED_SIZE + static_cast<size_t>(point_count) * 4);

    size_t off = 0;

    auto write32 = [&](const uint32_t v) {
        const uint32_t n = htonl(v);
        std::memcpy(buf.data() + off, &n, 4);
        off += 4;
    };
    auto write16 = [&](const uint16_t v) {
        const uint16_t n = htons(v);
        std::memcpy(buf.data() + off, &n, 2);
        off += 2;
    };

    write32(op.seq);
    write32(op.member_id);
    buf[off++] = static_cast<uint8_t>(op.tool);
    write32(op.color);
    buf[off++] = op.thickness;
    write16(point_count);

    for (const auto& p : op.points)
    {
        write16(p.x);
        write16(p.y);
    }

    return buf;
}

result<draw_operation> parseDrawOperation(const std::span<const uint8_t> data)
{
    if (data.size() < DRAW_OP_FIXED_SIZE)
        return {.value = {}, .err = error::malformed, .message = "draw_operation too short"};

    draw_operation op;
    size_t off = 0;

    auto read32 = [&]() -> uint32_t {
        uint32_t v;
        std::memcpy(&v, data.data() + off, 4);
        off += 4;
        return ntohl(v);
    };
    auto read16 = [&]() -> uint16_t {
        uint16_t v;
        std::memcpy(&v, data.data() + off, 2);
        off += 2;
        return ntohs(v);
    };

    op.seq       = read32();
    op.member_id = read32();
    op.tool      = static_cast<tool_type>(data[off++]);
    op.color     = read32();
    op.thickness = data[off++];

    const uint16_t point_count = read16();

    if (data.size() < DRAW_OP_FIXED_SIZE + static_cast<size_t>(point_count) * 4)
        return {.value = {}, .err = error::malformed, .message = "points truncated"};

    op.points.resize(point_count);
    for (auto& p : op.points)
    {
        p.x = read16();
        p.y = read16();
    }

    return {.value = std::move(op), .err = error::none};
}
