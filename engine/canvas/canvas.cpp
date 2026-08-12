#include "canvas.h"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>

uint32_t canvas::get_width() const
{
    std::lock_guard lock(operations_mutex);
    return width;
}

uint32_t canvas::get_height() const
{
    std::lock_guard lock(operations_mutex);
    return height;
}

std::vector<uint32_t> canvas::copy_pixels() const
{
    std::lock_guard lock(raster_mutex);
    return pixels;
}

std::vector<uint32_t> canvas::copy_pixels_with_preview(const draw_operation& op) const
{
    std::lock_guard lock(raster_mutex);
    canvas preview;
    preview.width = width;
    preview.height = height;
    preview.background = background;
    preview.pixels = pixels;
    preview.rasterize(op);
    return std::move(preview.pixels);
}

uint32_t canvas::append(draw_operation op)
{
    return apply(std::move(op));
}

std::vector<draw_operation> canvas::snapshot() const
{
    std::lock_guard lock(operations_mutex);
    return operations;
}

bool canvas::contains_operation(const uint64_t operation_id) const
{
    if (operation_id == 0)
        return false;
    std::lock_guard lock(operations_mutex);
    return operation_ids.contains(operation_id);
}

void canvas::create(const uint32_t new_width, const uint32_t new_height, const uint32_t new_background)
{
    std::lock_guard raster_lock(raster_mutex);
    std::lock_guard operations_lock(operations_mutex);
    width = new_width;
    height = new_height;
    background = new_background;
    pixels.assign(static_cast<size_t>(width) * height, background);
    operations.clear();
    operation_ids.clear();
    next_seq.store(1);
}

uint32_t canvas::apply(draw_operation op)
{
    if (op.operation_id != 0)
    {
        std::lock_guard lock(operations_mutex);
        for (const auto& existing : operations)
            if (existing.operation_id == op.operation_id)
                return existing.seq;
    }

    const uint32_t seq = op.seq == 0 ? next_seq.fetch_add(1) : op.seq;
    op.seq = seq;

    std::lock_guard raster_lock(raster_mutex);
    {
        std::lock_guard lock(operations_mutex);
        if (op.operation_id != 0 && operation_ids.contains(op.operation_id))
            return seq;
    }
    if (seq >= next_seq.load())
        next_seq.store(seq + 1);

    rasterize(op);
    {
        std::lock_guard lock(operations_mutex);
        operations.push_back(op);
        if (op.operation_id != 0)
            operation_ids.insert(op.operation_id);
    }
    return seq;
}

void canvas::load(const std::vector<draw_operation>& ops)
{
    std::lock_guard raster_lock(raster_mutex);
    std::lock_guard operations_lock(operations_mutex);
    operations = ops;
    operation_ids.clear();
    for (const auto& op : operations)
        if (op.operation_id != 0)
            operation_ids.insert(op.operation_id);

    next_seq.store(1);
    for (const auto& op : operations)
        next_seq.store(std::max(next_seq.load(), op.seq + 1));

    if (!pixels.empty())
        std::ranges::fill(pixels, background);

    for (const auto& op : operations)
        rasterize(op);
}

void canvas::rasterize(const draw_operation& op)
{
    if (op.tool == tool_type::clear)
    {
        std::ranges::fill(pixels, background);
        return;
    }
    if (width == 0 || height == 0 || op.points.empty())
        return;

    const auto point = [this](const canvas_point p) {
        return std::pair{
            static_cast<int>((static_cast<uint64_t>(p.x) * (width - 1)) / 65535),
            static_cast<int>((static_cast<uint64_t>(p.y) * (height - 1)) / 65535)
        };
    };

    const auto put = [this, &op](const int x, const int y) {
        const int radius = std::max(0, static_cast<int>(op.thickness) / 2);
        const uint32_t color = op.tool == tool_type::eraser ? 0xFFFFFFFF : op.color;
        for (int dy = -radius; dy <= radius; ++dy)
        {
            for (int dx = -radius; dx <= radius; ++dx)
            {
                if (dx * dx + dy * dy > radius * radius)
                    continue;
                const int px = x + dx;
                const int py = y + dy;
                if (px >= 0 && py >= 0 && static_cast<uint32_t>(px) < width && static_cast<uint32_t>(py) < height)
                    pixels[static_cast<size_t>(py) * width + px] = color;
            }
        }
    };

    auto [x0, y0] = point(op.points.front());
    auto [x1, y1] = point(op.points.back());

    const auto line = [&put](int ax, int ay, const int bx, const int by) {
        int dx = std::abs(bx - ax);
        const int sx = ax < bx ? 1 : -1;
        int dy = -std::abs(by - ay);
        const int sy = ay < by ? 1 : -1;
        int error = dx + dy;
        while (true)
        {
            put(ax, ay);
            if (ax == bx && ay == by)
                break;
            const int doubled = 2 * error;
            if (doubled >= dy) { error += dy; ax += sx; }
            if (doubled <= dx) { error += dx; ay += sy; }
        }
    };

    if (op.tool == tool_type::freehand || op.tool == tool_type::eraser || op.tool == tool_type::line)
    {
        for (size_t i = op.tool == tool_type::line ? op.points.size() - 1 : 1; i < op.points.size(); ++i)
        {
            auto [x, y] = point(op.points[i]);
            line(x0, y0, x, y);
            x0 = x;
            y0 = y;
        }
    }
    else if (op.tool == tool_type::rect || op.tool == tool_type::filled_rect)
    {
        if (op.tool == tool_type::filled_rect)
        {
            for (int y = std::min(y0, y1); y <= std::max(y0, y1); ++y)
                for (int x = std::min(x0, x1); x <= std::max(x0, x1); ++x)
                    put(x, y);
            return;
        }
        line(x0, y0, x1, y0);
        line(x1, y0, x1, y1);
        line(x1, y1, x0, y1);
        line(x0, y1, x0, y0);
    }
    else if ((op.tool == tool_type::ellipse || op.tool == tool_type::filled_ellipse) && op.points.size() > 1)
    {
        const int left = std::min(x0, x1);
        const int right = std::max(x0, x1);
        const int top = std::min(y0, y1);
        const int bottom = std::max(y0, y1);
        const int cx = (left + right) / 2;
        const int cy = (top + bottom) / 2;
        const int rx = std::max(1, (right - left) / 2);
        const int ry = std::max(1, (bottom - top) / 2);
        if (op.tool == tool_type::filled_ellipse)
        {
            for (int y = -ry; y <= ry; ++y)
            {
                const int span = static_cast<int>(rx * std::sqrt(
                    std::max(0.0, 1.0 - (static_cast<double>(y) * y) / (static_cast<double>(ry) * ry))));
                for (int x = -span; x <= span; ++x)
                    put(cx + x, cy + y);
            }
            return;
        }
        long long rx2 = static_cast<long long>(rx) * rx;
        long long ry2 = static_cast<long long>(ry) * ry;
        long long x = 0;
        long long y = ry;
        long long px = 0;
        long long py = 2 * rx2 * y;
        double decision = ry2 - rx2 * ry + 0.25 * rx2;
        while (px < py)
        {
            put(cx + static_cast<int>(x), cy + static_cast<int>(y));
            put(cx - static_cast<int>(x), cy + static_cast<int>(y));
            put(cx - static_cast<int>(x), cy - static_cast<int>(y));
            put(cx + static_cast<int>(x), cy - static_cast<int>(y));
            ++x;
            px += 2 * ry2;
            if (decision < 0)
                decision += ry2 + px;
            else
            {
                --y;
                py -= 2 * rx2;
                decision += ry2 + px - py;
            }
        }
        decision = ry2 * (x + 0.5) * (x + 0.5) + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
        while (y >= 0)
        {
            put(cx + static_cast<int>(x), cy + static_cast<int>(y));
            put(cx - static_cast<int>(x), cy + static_cast<int>(y));
            put(cx - static_cast<int>(x), cy - static_cast<int>(y));
            put(cx + static_cast<int>(x), cy - static_cast<int>(y));
            --y;
            py -= 2 * rx2;
            if (decision > 0)
                decision += rx2 - py;
            else
            {
                ++x;
                px += 2 * ry2;
                decision += rx2 - py + px;
            }
        }
    }
}

std::optional<draw_operation> process_canvas_input(
    canvas& surface,
    std::optional<draw_operation>& temporary,
    const canvas_input_state& input,
    const uint64_t operation_id,
    const uint32_t member_id,
    const uint32_t color,
    const uint8_t thickness,
    const bool commit_to_canvas,
    const tool_type tool)
{
    if (!temporary && input.pressed && input.inside)
    {
        temporary.emplace();
        temporary->operation_id = operation_id;
        temporary->member_id = member_id;
        temporary->tool = tool;
        temporary->color = color;
        temporary->thickness = thickness;
        temporary->points.push_back(input.position);
    }

    if (temporary && (input.down || (input.released && input.inside)))
    {
        auto& points = temporary->points;
        if (tool != tool_type::freehand && tool != tool_type::eraser)
        {
            if (points.size() == 1)
                points.push_back(input.position);
            else if (!points.empty())
                points.back() = input.position;
        }
        else
        {
        const int dx = points.empty() ? 0 : static_cast<int>(input.position.x) - points.back().x;
        const int dy = points.empty() ? 0 : static_cast<int>(input.position.y) - points.back().y;
        if (points.empty() || dx * dx + dy * dy >= 32 * 32)
            temporary->points.push_back(input.position);
        }
    }

    if (!temporary || !input.released)
        return std::nullopt;

    draw_operation committed = std::move(*temporary);
    temporary.reset();
    if (committed.points.size() == 1)
        committed.points.push_back(committed.points.front());
    if (commit_to_canvas)
        committed.seq = surface.apply(committed);
    return committed;
}
