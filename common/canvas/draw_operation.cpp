#include "common/canvas/draw_operation.h"
#include "common/bytes.h"

#include <cstddef>

std::vector<uint8_t> serializeDrawOperation(const draw_operation& op)
{
    const auto point_count = static_cast<uint16_t>(op.points.size());
    std::vector<uint8_t> buf(DRAW_OP_FIXED_SIZE + static_cast<size_t>(point_count) * 4);
    size_t off = 0;

    bytes::write64(buf, off, op.operation_id);
    bytes::write32(buf, off, op.seq);
    bytes::write32(buf, off, op.member_id);
    bytes::write8(buf, off, static_cast<uint8_t>(op.tool));
    bytes::write32(buf, off, op.color);
    bytes::write8(buf, off, op.thickness);
    bytes::write16(buf, off, point_count);

    for (const auto& [x, y] : op.points)
    {
        bytes::write16(buf, off, x);
        bytes::write16(buf, off, y);
    }

    return buf;
}

result<draw_operation> parseDrawOperation(const std::span<const uint8_t> data)
{
    if (data.size() < DRAW_OP_FIXED_SIZE)
        return {.value = {}, .err = error::malformed, .message = "draw_operation too short"};

    draw_operation op;
    size_t off = 0;

    op.operation_id = bytes::read64(data, off);
    op.seq = bytes::read32(data, off);
    op.member_id = bytes::read32(data, off);
    op.tool = static_cast<tool_type>(bytes::read8(data, off));
    op.color = bytes::read32(data, off);
    op.thickness = bytes::read8(data, off);

    const uint16_t point_count = bytes::read16(data, off);

    if (data.size() < DRAW_OP_FIXED_SIZE + static_cast<size_t>(point_count) * 4)
        return {.value = {}, .err = error::malformed, .message = "points truncated"};

    op.points.resize(point_count);
    for (auto& [x, y] : op.points)
    {
        x = bytes::read16(data, off);
        y = bytes::read16(data, off);
    }

    return {.value = std::move(op), .err = error::none};
}

result<bool> validateDrawOperation(const draw_operation& op, const bool require_sequence)
{
    constexpr size_t max_points = 65535;
    constexpr uint8_t max_thickness = 64;

    if (op.operation_id == 0)
        return {.value = false, .err = error::malformed, .message = "operation ID is missing"};
    if (require_sequence ? op.seq == 0 : op.seq != 0)
        return {.value = false, .err = error::malformed, .message = require_sequence
                    ? "committed operation has no sequence"
                    : "uncommitted operation has a sequence"};
    if (op.member_id == 0)
        return {.value = false, .err = error::malformed, .message = "member ID is missing"};
    if (static_cast<uint8_t>(op.tool) > static_cast<uint8_t>(tool_type::filled_ellipse))
        return {.value = false, .err = error::malformed, .message = "unknown drawing tool"};
    if (op.thickness == 0 || op.thickness > max_thickness)
        return {.value = false, .err = error::malformed, .message = "invalid stroke thickness"};
    if (op.points.size() > max_points)
        return {.value = false, .err = error::malformed, .message = "too many points"};

    if (op.tool == tool_type::clear)
    {
        if (!op.points.empty())
            return {.value = false, .err = error::malformed, .message = "clear operation has points"};
    }
    else if ((op.tool == tool_type::line || op.tool == tool_type::rect || op.tool == tool_type::ellipse ||
              op.tool == tool_type::filled_rect || op.tool == tool_type::filled_ellipse) &&
             op.points.size() != 2)
        return {.value = false, .err = error::malformed, .message = "shape requires two points"};
    else if ((op.tool == tool_type::freehand || op.tool == tool_type::eraser) && op.points.empty())
        return {.value = false, .err = error::malformed, .message = "stroke has no points"};

    return {.value = true, .err = error::none};
}
