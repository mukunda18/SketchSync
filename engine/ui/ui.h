#ifndef SKETCHSYNC_UI_H
#define SKETCHSYNC_UI_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef NOGDI
#define NOGDI
#endif

#include "raylib.h"

#include "common/canvas/draw_operation.h"
#include "engine/canvas/canvas.h"

namespace ui
{
    constexpr int WINDOW_WIDTH = 1280;
    constexpr int WINDOW_HEIGHT = 800;
    constexpr int TOP_BAR_HEIGHT = 56;

    constexpr uint32_t HOST_COLOR = 0xFF1F1F1F;
    constexpr uint32_t BACKGROUND_COLOR = 0xFFFFFFFF;

    extern Font default_font;

    void close_window();

    Color argb_to_color(uint32_t argb);
    canvas_point normalize_point(Vector2 point, struct Rectangle canvas_rect);
    Vector2 clamp_to_canvas(Vector2 point, struct Rectangle canvas_rect);
    draw_operation begin_freehand(canvas_point start);
    void append_point(draw_operation& op, canvas_point point);
    bool has_points(const draw_operation& op);

    bool button_hit(struct Rectangle rect);
    void draw_button(struct Rectangle rect, const char* label, bool active = false);

    bool load_binary_replay(const std::filesystem::path& path, canvas& surface, std::string& status);
    void sync_texture(const Texture2D& texture, const std::vector<uint32_t>& pixels,
                      std::vector<Color>& upload_buffer);
}

#endif
