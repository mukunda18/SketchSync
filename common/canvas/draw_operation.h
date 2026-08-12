#ifndef SKETCHSYNC_DRAW_OPERATION_H
#define SKETCHSYNC_DRAW_OPERATION_H

#include <cstdint>
#include <span>
#include <vector>
#include "common/results.h"

enum class tool_type : uint8_t
{
    freehand = 0x00,
    line = 0x01,
    rect = 0x02,
    ellipse = 0x03,
    eraser = 0x04,
    clear = 0x05,
    filled_rect = 0x06,
    filled_ellipse = 0x07,
};

struct canvas_point
{
    uint16_t x; // normalized 0–65535 maps to 0.0–1.0
    uint16_t y;
};

struct draw_operation
{
    uint64_t operation_id = 0; // client-generated ID used for deduplication
    uint32_t seq = 0; // 0 until the host stamps it
    uint32_t member_id = 0;
    tool_type tool = tool_type::freehand;
    uint32_t color = 0xFFFFFFFF; // ARGB
    uint8_t thickness = 1;
    std::vector<canvas_point> points;
    // LINE:   points[0]=start,   points[1]=end
    // ELLIPSE: points[0]=drag start, points[1]=drag end; midpoint is center
    // RECT:   points[0]=top-left, points[1]=bottom-right
    // CIRCLE: points[0]=center,  points[1]={radius, 0}
};

// seq(4) + member_id(4) + tool(1) + color(4) + thickness(1) + point_count(2)
static constexpr size_t DRAW_OP_FIXED_SIZE = 24;

std::vector<uint8_t> serializeDrawOperation(const draw_operation& op);
result<draw_operation> parseDrawOperation(std::span<const uint8_t> data);
result<bool> validateDrawOperation(const draw_operation& op, bool require_sequence);

#endif
