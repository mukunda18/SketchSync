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
    Font default_font{};


    void close_window()
    {
        ::CloseWindow();
    }

    Color argb_to_color(const uint32_t argb)
    {
        return Color{
            .r = static_cast<unsigned char>(argb & 0xFF),
            .g = static_cast<unsigned char>((argb >> 8) & 0xFF),
            .b = static_cast<unsigned char>((argb >> 16) & 0xFF),
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

    bool load_binary_replay(const std::filesystem::path& path, canvas& surface, std::string& status, uint32_t& saved_seq)
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
        saved_seq = 0;

        constexpr std::array<uint8_t, 4> SKSY_MAGIC{'S', 'K', 'S', 'Y'};
        if (file_data.size() < 16 || !std::equal(SKSY_MAGIC.begin(), SKSY_MAGIC.end(), file_data.begin()))
        {
            status = "Invalid or unsupported SketchSync file format";
            return false;
        }

        size_t off = 4;
        bytes::read32(data, off);          // skip version field
        saved_seq = bytes::read32(data, off);
        bytes::read32(data, off);          // skip reserved field
        offset = 16;

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

        if (offset != file_data.size())
        {
            status = "Unexpected trailing bytes";
            return false;
        }

        surface.load(operations);
        status = "Loaded " + std::to_string(operations.size()) + " draw ops";
        return true;
    }

    uint32_t read_saved_seq(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            return 0;
        }

        std::array<uint8_t, 16> header_buf{};
        file.read(reinterpret_cast<char*>(header_buf.data()), header_buf.size());
        if (file.gcount() < 16)
        {
            return 0;
        }

        constexpr std::array<uint8_t, 4> SKSY_MAGIC{'S', 'K', 'S', 'Y'};
        if (std::equal(SKSY_MAGIC.begin(), SKSY_MAGIC.end(), header_buf.begin()))
        {
            const std::span<const uint8_t> data(header_buf.data(), header_buf.size());
            size_t off = 8;
            return bytes::read32(data, off);
        }
        return 0;
    }


    void sync_texture(const Texture2D& texture, const std::vector<uint32_t>& pixels)
    {
        if (!pixels.empty())
            UpdateTexture(texture, pixels.data());
    }

}
