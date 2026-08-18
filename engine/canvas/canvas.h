#ifndef SKETCHSYNC_CANVAS_H
#define SKETCHSYNC_CANVAS_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <vector>

#include "common/canvas/draw_operation.h"

struct canvas
{
    canvas() = default;

    [[nodiscard]] uint32_t get_width() const;
    [[nodiscard]] uint32_t get_height() const;
    [[nodiscard]] std::vector<uint32_t> copy_pixels() const;
    [[nodiscard]] std::vector<uint32_t> copy_pixels_with_preview(const draw_operation& op) const;

    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t background = 0xFFFFFFFF;
    std::vector<uint32_t> pixels;
    std::atomic<uint32_t> next_seq{1};
    mutable std::mutex operations_mutex;
    mutable std::mutex raster_mutex;
    std::vector<draw_operation> operations;
    std::unordered_set<uint64_t> operation_ids;
    std::vector<draw_operation> snapshot() const;
    [[nodiscard]] bool contains_operation(uint64_t operation_id) const;
    [[nodiscard]] uint32_t next_sequence() const noexcept { return next_seq.load(); }
    void create(uint32_t new_width, uint32_t new_height, uint32_t background = 0xFFFFFFFF);
    uint32_t apply(draw_operation op);
    void load(const std::vector<draw_operation>& ops);

private:
    void rasterize(const draw_operation& op);
};

struct canvas_input_state
{
    bool inside = false;
    bool pressed = false;
    bool down = false;
    bool released = false;
    canvas_point position{};
    tool_type tool = tool_type::freehand;
};

std::optional<draw_operation> process_canvas_input(
    canvas& surface,
    std::optional<draw_operation>& temporary,
    const canvas_input_state& input,
    uint64_t operation_id = 0,
    uint32_t member_id = 0,
    uint32_t color = 0xFF1F1F1F,
    uint8_t thickness = 2,
    bool commit_to_canvas = true,
    tool_type tool = tool_type::freehand);

#endif
