#include "engine/engine.h"

#include <algorithm>
#include <array>

#include "engine/ui/ui.h"
#include "common/network_constants.h"

sketch_app::sketch_app()
    : files_(surface, [this](std::string value) { set_status(std::move(value)); }),
      net_([this](std::string value) { set_status(std::move(value)); }),
      session_(surface, files_, net_, dirty, [this](std::string value) { set_status(std::move(value)); }),
      layout()
{
    net_.set_session(session_);
    surface.create(ui::WINDOW_WIDTH, ui::WINDOW_HEIGHT - ui::TOP_BAR_HEIGHT, ui::BACKGROUND_COLOR);
}

sketch_app::~sketch_app()
{
    shutdown();
}

void sketch_app::shutdown()
{
    if (shut_down_.exchange(true))
        return;
    net_.shutdown();
    files_.shutdown();
    if (canvas_texture.id != 0)
    {
        UnloadTexture(canvas_texture);
        canvas_texture.id = 0;
    }
}

void sketch_app::rebuild_render_texture() const
{
    if (canvas_texture.id == 0)
        return;
    ui::sync_texture(canvas_texture, surface.copy_pixels());
}

void sketch_app::set_status(std::string value)
{
    std::lock_guard lock(status_mutex);
    status = std::move(value);
}

std::string sketch_app::get_status() const
{
    std::lock_guard lock(status_mutex);
    return status;
}

void sketch_app::clear_canvas()
{
    active_stroke.reset();
    const uint64_t operation_id = session_.next_operation_id();
    const auto member = static_cast<uint32_t>(operation_id >> 32);
    const bool host_owns_canvas = session_.host_owns_canvas();

    draw_operation clear_op;
    clear_op.operation_id = operation_id;
    clear_op.member_id = member;
    clear_op.tool = tool_type::clear;
    clear_op.color = 0xFFFFFFFF;
    clear_op.thickness = 1;

    if (host_owns_canvas)
    {
        surface.apply(clear_op);
        files_.enqueue_if_auto_save(clear_op);
        rebuild_render_texture();
        dirty.store(true);
    }

    session_.broadcast_draw(clear_op, !host_owns_canvas);
    set_status("Canvas cleared");
}

void sketch_app::process_canvas_input(const canvas_input_state& input)
{
    const uint64_t operation_id = session_.next_operation_id();
    const auto member = static_cast<uint32_t>(operation_id >> 32);
    const bool host_owns_canvas = session_.host_owns_canvas();
    const auto committed = ::process_canvas_input(surface, active_stroke, input, operation_id, member, active_color, active_thickness, host_owns_canvas, active_tool);
    if (!committed)
        return;

    if (host_owns_canvas)
        files_.enqueue_if_auto_save(*committed);
    dirty.store(true);
    session_.broadcast_draw(*committed, !host_owns_canvas);
}

static Vector2 canvas_to_screen(const canvas_point p, const Rectangle& r)
{
    return Vector2{
        .x = r.x + (static_cast<float>(p.x) / 65535.0f) * r.width,
        .y = r.y + (static_cast<float>(p.y) / 65535.0f) * r.height
    };
}

static bool draw_stroke_preview(const draw_operation& op, const Rectangle& canvas_rect)
{
    if (op.points.size() < 2)
        return false;

    const Vector2 p0 = canvas_to_screen(op.points.front(), canvas_rect);
    const Vector2 p1 = canvas_to_screen(op.points.back(),  canvas_rect);
    const Color   c  = ui::argb_to_color(op.color);

    const float thickness = std::max(1.0f,
        static_cast<float>(op.thickness) * canvas_rect.width / static_cast<float>(1280));

    switch (op.tool)
    {
    case tool_type::line:
        DrawLineEx(p0, p1, thickness, c);
        return true;

    case tool_type::rect:
    {
        const float x = std::min(p0.x, p1.x);
        const float y = std::min(p0.y, p1.y);
        const float w = std::abs(p1.x - p0.x);
        const float h = std::abs(p1.y - p0.y);
        DrawRectangleLinesEx({.x = x, .y = y, .width = w, .height = h}, thickness, c);
        return true;
    }

    case tool_type::filled_rect:
    {
        const float x = std::min(p0.x, p1.x);
        const float y = std::min(p0.y, p1.y);
        const float w = std::abs(p1.x - p0.x);
        const float h = std::abs(p1.y - p0.y);
        DrawRectangleRec({.x = x, .y = y, .width = w, .height = h}, c);
        return true;
    }

    case tool_type::ellipse:
    {
        const float cx = (p0.x + p1.x) * 0.5f;
        const float cy = (p0.y + p1.y) * 0.5f;
        const float rx = std::max(1.0f, std::abs(p1.x - p0.x) * 0.5f);
        const float ry = std::max(1.0f, std::abs(p1.y - p0.y) * 0.5f);
        DrawEllipseLines(static_cast<int>(cx), static_cast<int>(cy),
                         rx, ry, c);
        return true;
    }

    case tool_type::filled_ellipse:
    {
        const float cx = (p0.x + p1.x) * 0.5f;
        const float cy = (p0.y + p1.y) * 0.5f;
        const float rx = std::max(1.0f, std::abs(p1.x - p0.x) * 0.5f);
        const float ry = std::max(1.0f, std::abs(p1.y - p0.y) * 0.5f);
        DrawEllipse(static_cast<int>(cx), static_cast<int>(cy), rx, ry, c);
        return true;
    }

    default:
        return false;
    }
}

int sketch_app::run()
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(ui::WINDOW_WIDTH, ui::WINDOW_HEIGHT, "SketchSync");
    SetWindowMinSize(1024, 768);

    ui::default_font = LoadFontEx("../engine/ui/ClarityCity-Black.otf", 64, nullptr, 0);

    if (!IsFontValid(ui::default_font))
        set_status("Warning: ClarityCity-Black.otf not found at ../engine/ui/");

    canvas_texture = LoadTextureFromImage(GenImageColor(static_cast<int>(surface.width), static_cast<int>(surface.height), WHITE));
    rebuild_render_texture();
    SetTargetFPS(60);

    layout.host_field.value = &net_.host();
    layout.port_field.value = &net_.port();
    layout.session_id_field.value = &session_.session_id_input();

    while (!WindowShouldClose())
    {
        const auto ww = static_cast<float>(GetScreenWidth()), wh = static_cast<float>(GetScreenHeight());
        const connection_protocol current_proto = net_.protocol();
        const bool is_connected = net_.connected();
        const bool is_running = net_.local_server_running();

        layout.update_layout(ww, wh, current_proto);

        if (layout.open_btn.update())
        {
            files_.open_and_load();
            active_stroke.reset();
            dirty.store(true);
        }
        if (layout.save_btn.update())
        {
            if (files_.is_untitled())
                files_.save_as();
            else
                files_.save_to_file(files_.current_file());
        }
        if (layout.save_as_btn.update())
            files_.save_as();
        if (layout.auto_save_btn.update())
            files_.toggle_auto_save();
        if (layout.clear_btn.update())
            clear_canvas();

        if (files_.consume_synced_save())
            files_.save_to_file(files_.current_file());

        if (layout.connect_btn.update())
        {
            if (is_connected)
                net_.disconnect();
            else if (current_proto == connection_protocol::websocket)
                net_.async_connect_to_server();
            else
                set_status("Enter Session ID and click Join to discover host, or click Start Local");
        }

        if (layout.local_server_btn.update())
        {
            if (is_running)
                net_.stop_local_server();
            else
                net_.async_start_local_server();
        }

        if (layout.protocol_toggle.update())
        {
            if (!net_.toggle_protocol())
                set_status("Disconnect before switching protocol");
        }

        if (!session_.in_session())
        {
            if (layout.join_btn.update())
                session_.join_session();
            if (layout.create_btn.update())
                session_.create_session();
        }
        else
        {
            if (layout.leave_btn.update())
                session_.leave_session();
        }

        if (current_proto == connection_protocol::websocket)
        {
            layout.host_field.update();
            layout.port_field.update();
        }
        layout.session_id_field.update();

        for (auto& tb : layout.tool_buttons)
        {
            tb.selected = (active_tool == static_cast<tool_type>(tb.tool_id));
            if (tb.update())
                active_tool = static_cast<tool_type>(tb.tool_id);
        }

        for (size_t i = 0; i < layout.thickness_buttons.size(); ++i)
        {
            constexpr std::array<uint8_t, 4> thicknesses{1, 2, 4, 8};
            layout.thickness_buttons[i].active = (active_thickness == thicknesses[i]);
            if (layout.thickness_buttons[i].update())
                active_thickness = thicknesses[i];
        }

        for (auto& s : layout.color_swatches)
        {
            s.selected = (active_color == s.color);
            if (s.update())
                active_color = s.color;
        }

        const float canvas_area_w = ww - layout.left_panel_width - layout.right_panel_width;
        const float canvas_area_h = wh - layout.top_bar_height - layout.bottom_panel_height;
        float cw = canvas_area_w * 0.95f, ch = cw / (static_cast<float>(surface.width) / static_cast<float>(surface.height));
        if (ch > canvas_area_h * 0.95f)
        {
            ch = canvas_area_h * 0.95f;
            cw = ch * (static_cast<float>(surface.width) / static_cast<float>(surface.height));
        }
        const Rectangle canvas_rect = {
            .x = layout.left_panel_width + (canvas_area_w - cw) * 0.5f,
            .y = layout.top_bar_height + (canvas_area_h - ch) * 0.5f,
            .width = cw,
            .height = ch
        };

        if (!IsWindowMinimized())
        {
            const Vector2 mouse = GetMousePosition();
            process_canvas_input({
                .inside = CheckCollisionPointRec(mouse, canvas_rect),
                .pressed = IsMouseButtonPressed(MOUSE_LEFT_BUTTON),
                .down = IsMouseButtonDown(MOUSE_LEFT_BUTTON),
                .released = IsMouseButtonReleased(MOUSE_LEFT_BUTTON),
                .position = ui::normalize_point(ui::clamp_to_canvas(mouse, canvas_rect), canvas_rect),
                .tool = active_tool
            });
        }
        if (dirty.load())
        {
            rebuild_render_texture();
            dirty.store(false);
        }
        if (active_stroke.has_value())
        {
            const tool_type t = active_stroke->tool;
            const bool is_pixel_tool = (t == tool_type::freehand ||
                                        t == tool_type::brush    ||
                                        t == tool_type::eraser);
            if (is_pixel_tool)
                ui::sync_texture(canvas_texture,
                                 surface.copy_pixels_with_preview(*active_stroke));
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);
        layout.draw(
            current_proto,
            is_connected,
            net_.state(),
            net_.host(),
            net_.port(),
            session_.in_session(),
            session_.session_id(),
            session_.member_id(),
            get_status(),
            files_.current_file(),
            is_running,
            files_.auto_save_on());

        DrawTexturePro(canvas_texture,
            {.x = 0, .y = 0,
             .width  = static_cast<float>(canvas_texture.width),
             .height = static_cast<float>(canvas_texture.height)},
            canvas_rect, {.x = 0, .y = 0}, 0, WHITE);

        if (active_stroke.has_value())
            draw_stroke_preview(*active_stroke, canvas_rect);

        DrawRectangleLinesEx(canvas_rect, 1, DARKGRAY);
        EndDrawing();
    }
    shutdown();
    if (IsFontValid(ui::default_font))
        UnloadFont(ui::default_font);
    ui::close_window();
    return 0;
}
