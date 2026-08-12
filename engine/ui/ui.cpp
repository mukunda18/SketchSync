#include "engine/ui/ui.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <fstream>
#include <iterator>

#include "common/bytes.h"

#include "common/protocol/message.h"

namespace ui
{
    namespace
    {
        constexpr std::array<uint8_t, 4> DRAW_LOG_MAGIC{'S', 'S', 'D', 'O'};
    }

    void close_window()
    {
        // The global qualification selects raylib's zero-argument API and
        // avoids ambiguity with Win32's CloseWindow(HWND).
        ::CloseWindow();
    }

    Color argb_to_color(const uint32_t argb)
    {
        return Color{
            .r = static_cast<unsigned char>((argb >> 16) & 0xFF),
            .g = static_cast<unsigned char>((argb >> 8) & 0xFF),
            .b = static_cast<unsigned char>(argb & 0xFF),
            .a = static_cast<unsigned char>((argb >> 24) & 0xFF)
        };
    }

    canvas_point normalize_point(const Vector2 point, const struct Rectangle canvas_rect)
    {
        const float x = std::clamp((point.x - canvas_rect.x) / canvas_rect.width, 0.0f, 1.0f);
        const float y = std::clamp((point.y - canvas_rect.y) / canvas_rect.height, 0.0f, 1.0f);
        return canvas_point{
            .x = static_cast<uint16_t>(x * 65535.0f),
            .y = static_cast<uint16_t>(y * 65535.0f)
        };
    }

    Vector2 clamp_to_canvas(const Vector2 point, const struct Rectangle canvas_rect)
    {
        return Vector2{
            .x = std::clamp(point.x, canvas_rect.x, canvas_rect.x + canvas_rect.width),
            .y = std::clamp(point.y, canvas_rect.y, canvas_rect.y + canvas_rect.height)
        };
    }

    ::draw_operation begin_freehand(const canvas_point start)
    {
        ::draw_operation op;
        op.member_id = 1;
        op.tool = tool_type::freehand;
        op.color = HOST_COLOR;
        op.thickness = 2;
        op.points.push_back(start);
        return op;
    }

    void append_point(::draw_operation& op, const canvas_point point)
    {
        if (op.points.empty() || op.points.back().x != point.x || op.points.back().y != point.y)
            op.points.push_back(point);
    }

    bool has_points(const ::draw_operation& op)
    {
        return !op.points.empty();
    }

    bool button_hit(const struct Rectangle rect)
    {
        return CheckCollisionPointRec(GetMousePosition(), rect) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
    }

    void draw_button(const struct Rectangle rect, const char* label, const bool active)
    {
        const Vector2 mouse = GetMousePosition();
        const bool hovered = CheckCollisionPointRec(mouse, rect);
        Color fill;
        if (active && hovered)
            fill = Color{.r = 82, .g = 140, .b = 240, .a = 255};
        else if (active)
            fill = Color{.r = 52, .g = 120, .b = 220, .a = 255};
        else if (hovered)
            fill = Color{.r = 98, .g = 102, .b = 114, .a = 255};
        else
            fill = Color{.r = 72, .g = 76, .b = 88, .a = 255};
        DrawRectangleRec(rect, fill);
        DrawRectangleLinesEx(rect, active ? 2.0f : 1.0f, Color{.r = 165, .g = 170, .b = 184, .a = 255});

        constexpr int text_size = 18;
        const int text_width = MeasureText(label, text_size);
        DrawText(label,
                 static_cast<int>(rect.x + (rect.width - static_cast<float>(text_width)) * 0.5f),
                 static_cast<int>(rect.y + (rect.height - static_cast<float>(text_size)) * 0.5f),
                 text_size,
                 RAYWHITE);
    }

    bool load_binary_replay(const std::filesystem::path& path, canvas& surface, std::string& status)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            status = "Failed to open file";
            return false;
        }

        const std::vector<uint8_t> file_data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const std::span<const uint8_t> data(file_data.data(), file_data.size());
        std::vector<draw_operation> operations;
        size_t offset = 0;

        if (file_data.size() >= DRAW_LOG_MAGIC.size() &&
            std::equal(DRAW_LOG_MAGIC.begin(), DRAW_LOG_MAGIC.end(), file_data.begin()))
        {
            offset = DRAW_LOG_MAGIC.size();

            while (offset + sizeof(uint32_t) <= file_data.size())
            {
                const uint32_t op_len = bytes::read32(data, offset);
                if (offset + op_len > file_data.size())
                {
                    status = "Truncated draw log";
                    return false;
                }

                auto op_res = parseDrawOperation(std::span<const uint8_t>(file_data.data() + offset, op_len));
                if (!op_res)
                {
                    status = op_res.message;
                    return false;
                }

                operations.push_back(std::move(op_res.value));
                offset += op_len;
            }
        }
        else
        {
            while (offset + Header::SIZE <= file_data.size())
            {
                const auto header_res = parseHeader(std::span<const uint8_t>(file_data.data() + offset, file_data.size() - offset));
                if (!header_res)
                {
                    status = header_res.message;
                    return false;
                }

                const Header header = header_res.value;
                offset += Header::SIZE;

                if (offset + header.length > file_data.size())
                {
                    status = "Truncated frame";
                    return false;
                }

                const auto payload = std::span<const uint8_t>(file_data.data() + offset, header.length);
                if (header.opcode == Opcode::DRAW)
                {
                    auto op_res = parseDrawOperation(payload);
                    if (!op_res)
                    {
                        status = op_res.message;
                        return false;
                    }

                    operations.push_back(op_res.value);
                }

                offset += header.length;
            }
        }

        if (offset != file_data.size())
        {
            status = "Unexpected trailing bytes";
            return false;
        }

        surface.load(operations);
        status = "Loaded " + std::to_string(operations.size()) + " draw ops";
        return true;
    }

    void sync_texture(Texture2D& texture, const std::vector<uint32_t>& pixels,
                      std::vector<Color>& upload_buffer)
    {
        upload_buffer.resize(pixels.size());
        for (size_t i = 0; i < pixels.size(); ++i)
            upload_buffer[i] = argb_to_color(pixels[i]);
        if (!upload_buffer.empty())
            UpdateTexture(texture, upload_buffer.data());
    }

}
