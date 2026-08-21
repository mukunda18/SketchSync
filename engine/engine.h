#ifndef SKETCHSYNC_ENGINE_H
#define SKETCHSYNC_ENGINE_H

#include <atomic>
#include <mutex>
#include <optional>
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
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#include "engine/network/network_manager.h"
#undef ShowCursor
#undef CloseWindow

#ifdef DrawText
#undef DrawText
#endif

#include "engine/canvas/canvas.h"
#include "engine/file/file_manager.h"
#include "engine/session/session_manager.h"
#include "engine/ui/app_layout.h"

struct sketch_app
{
    sketch_app();
    ~sketch_app();

    [[nodiscard]] int run();

private:
    void set_status(std::string value);
    [[nodiscard]] std::string get_status() const;
    void shutdown();
    void clear_canvas();
    void process_canvas_input(const canvas_input_state& input);
    void rebuild_render_texture() const;

    canvas surface;
    Texture2D canvas_texture{};
    std::string status = "Ready";
    std::atomic<bool> dirty{true};
    std::optional<draw_operation> active_stroke;
    tool_type active_tool = tool_type::freehand;
    uint32_t active_color = 0xFF1F1F1F;
    uint8_t active_thickness = 2;

    file_manager files_;
    network_manager net_;
    session_manager session_;
    ui::AppLayout layout;

    std::atomic<bool> shut_down_{false};
    mutable std::mutex status_mutex;
};

#endif
