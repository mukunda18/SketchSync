#include "canvas.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <ranges>
#include <utility>

namespace
{
    [[nodiscard]] uint8_t blend_channel(const uint8_t dst, const uint8_t src, const float src_alpha)
    {
        const float out = static_cast<float>(src) * src_alpha +
                          static_cast<float>(dst) * (1.0f - src_alpha);
        return static_cast<uint8_t>(std::clamp(out, 0.0f, 255.0f));
    }

    [[nodiscard]] uint32_t alpha_blend_over(const uint32_t dst_argb, const uint32_t src_argb, const float coverage)
    {
        const uint8_t src_a = static_cast<uint8_t>((src_argb >> 24) & 0xFF);
        if (src_a == 0 || coverage <= 0.0f)
            return dst_argb;

        const float src_alpha = (static_cast<float>(src_a) / 255.0f) * std::clamp(coverage, 0.0f, 1.0f);

        const uint8_t dst_a = static_cast<uint8_t>((dst_argb >> 24) & 0xFF);
        const uint8_t dst_r = static_cast<uint8_t>((dst_argb >> 16) & 0xFF);
        const uint8_t dst_g = static_cast<uint8_t>((dst_argb >> 8) & 0xFF);
        const uint8_t dst_b = static_cast<uint8_t>(dst_argb & 0xFF);

        const uint8_t src_r = static_cast<uint8_t>((src_argb >> 16) & 0xFF);
        const uint8_t src_g = static_cast<uint8_t>((src_argb >> 8) & 0xFF);
        const uint8_t src_b = static_cast<uint8_t>(src_argb & 0xFF);

        const float dst_alpha = static_cast<float>(dst_a) / 255.0f;
        const float out_alpha = src_alpha + dst_alpha * (1.0f - src_alpha);

        const uint8_t out_r = blend_channel(dst_r, src_r, src_alpha);
        const uint8_t out_g = blend_channel(dst_g, src_g, src_alpha);
        const uint8_t out_b = blend_channel(dst_b, src_b, src_alpha);
        const uint8_t out_a = static_cast<uint8_t>(std::clamp(out_alpha * 255.0f, 0.0f, 255.0f));

        return (static_cast<uint32_t>(out_a) << 24) |
               (static_cast<uint32_t>(out_r) << 16) |
               (static_cast<uint32_t>(out_g) << 8) |
               static_cast<uint32_t>(out_b);
    }
}

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

    const auto blend_pixel = [this](const int x, const int y, const uint32_t color, const float coverage) {
        if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= width || static_cast<uint32_t>(y) >= height)
            return;

        auto& dst = pixels[static_cast<size_t>(y) * width + x];
        dst = alpha_blend_over(dst, color, coverage);
    };

    const auto put = [this, &op, &blend_pixel](const float cx, const float cy) {
        const float radius = std::max(0.5f, static_cast<float>(op.thickness) * 0.5f);
        const uint32_t color = op.tool == tool_type::eraser ? background : op.color;
        const bool square_tip = op.tool == tool_type::brush;

        const int min_x = static_cast<int>(std::floor(cx - radius - 1.0f));
        const int max_x = static_cast<int>(std::ceil(cx + radius + 1.0f));
        const int min_y = static_cast<int>(std::floor(cy - radius - 1.0f));
        const int max_y = static_cast<int>(std::ceil(cy + radius + 1.0f));

        for (int y = min_y; y <= max_y; ++y)
        {
            for (int x = min_x; x <= max_x; ++x)
            {
                if (square_tip)
                {
                    const float edge_x = (radius + 0.5f) - std::abs((static_cast<float>(x) + 0.5f) - cx);
                    const float edge_y = (radius + 0.5f) - std::abs((static_cast<float>(y) + 0.5f) - cy);
                    const float coverage = std::clamp(std::min(edge_x, edge_y), 0.0f, 1.0f);
                    if (coverage > 0.0f)
                        blend_pixel(x, y, color, coverage);
                    continue;
                }

                const float dx = (static_cast<float>(x) + 0.5f) - cx;
                const float dy = (static_cast<float>(y) + 0.5f) - cy;
                const float dist = std::sqrt(dx * dx + dy * dy);
                const float coverage = std::clamp((radius + 0.5f) - dist, 0.0f, 1.0f);
                if (coverage > 0.0f)
                    blend_pixel(x, y, color, coverage);
            }
        }
    };

    auto [x0, y0] = point(op.points.front());
    auto [x1, y1] = point(op.points.back());

    const auto line = [&put](const float ax, const float ay, const float bx, const float by) {
        const float dx = bx - ax;
        const float dy = by - ay;
        const float distance = std::sqrt(dx * dx + dy * dy);
        const int steps = std::max(1, static_cast<int>(std::ceil(distance * 2.0f)));
        for (int i = 0; i <= steps; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            put(ax + dx * t, ay + dy * t);
        }
    };

    if (op.tool == tool_type::freehand || op.tool == tool_type::eraser || op.tool == tool_type::brush || op.tool == tool_type::line)
    {
        for (size_t i = op.tool == tool_type::line ? op.points.size() - 1 : 1; i < op.points.size(); ++i)
        {
            auto [x, y] = point(op.points[i]);
            line(static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x), static_cast<float>(y));
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
                    put(static_cast<float>(x), static_cast<float>(y));
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
            put(static_cast<float>(cx + static_cast<int>(x)), static_cast<float>(cy + static_cast<int>(y)));
            put(static_cast<float>(cx - static_cast<int>(x)), static_cast<float>(cy + static_cast<int>(y)));
            put(static_cast<float>(cx - static_cast<int>(x)), static_cast<float>(cy - static_cast<int>(y)));
            put(static_cast<float>(cx + static_cast<int>(x)), static_cast<float>(cy - static_cast<int>(y)));
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
            put(static_cast<float>(cx + static_cast<int>(x)), static_cast<float>(cy + static_cast<int>(y)));
            put(static_cast<float>(cx - static_cast<int>(x)), static_cast<float>(cy + static_cast<int>(y)));
            put(static_cast<float>(cx - static_cast<int>(x)), static_cast<float>(cy - static_cast<int>(y)));
            put(static_cast<float>(cx + static_cast<int>(x)), static_cast<float>(cy - static_cast<int>(y)));
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
    else if (op.tool == tool_type::bucket_fill && !op.points.empty())
    {
        auto [fx, fy] = point(op.points[0]);
        if (static_cast<uint32_t>(fx) >= width || static_cast<uint32_t>(fy) >= height)
            return;
        const uint32_t target = pixels[static_cast<size_t>(fy) * width + fx];
        if (target == op.color)
            return;
        pixels[static_cast<size_t>(fy) * width + fx] = op.color;
        std::queue<std::pair<int, int>> q;
        q.emplace(fx, fy);
        while (!q.empty())
        {
            auto [x, y] = q.front();
            q.pop();
            const auto try_fill = [&](const int nx, const int ny) {
                if (nx >= 0 && ny >= 0 &&
                    static_cast<uint32_t>(nx) < width && static_cast<uint32_t>(ny) < height &&
                    pixels[static_cast<size_t>(ny) * width + nx] == target)
                {
                    pixels[static_cast<size_t>(ny) * width + nx] = op.color;
                    q.emplace(nx, ny);
                }
            };
            try_fill(x + 1, y);
            try_fill(x - 1, y);
            try_fill(x, y + 1);
            try_fill(x, y - 1);
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
        if (tool == tool_type::freehand || tool == tool_type::eraser || tool == tool_type::brush)
        {
            const int dx = points.empty() ? 0 : static_cast<int>(input.position.x) - points.back().x;
            const int dy = points.empty() ? 0 : static_cast<int>(input.position.y) - points.back().y;
            if (points.empty() || dx * dx + dy * dy >= 32 * 32)
                temporary->points.push_back(input.position);
        }
        else if (tool != tool_type::bucket_fill)
        {
            // shape tools: track start + current end point
            if (points.size() == 1)
                points.push_back(input.position);
            else if (!points.empty())
                points.back() = input.position;
        }
        // bucket_fill: single click — no drag updates
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
