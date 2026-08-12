#include "engine/engine.h"

#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <utility>

#include "engine/client/sessionClient.h"
#include "engine/process/server_process.h"
#include "engine/persistence/persistenceWriter.h"
#include "engine/ui/file_dialog.h"
#include "engine/ui/ui.h"

sketch_app::sketch_app()
{
    surface.create(ui::WINDOW_WIDTH, ui::WINDOW_HEIGHT - ui::TOP_BAR_HEIGHT, ui::BACKGROUND_COLOR);
}

sketch_app::~sketch_app()
{
    stop_local_server();
    if (canvas_texture.id != 0)
        UnloadTexture(canvas_texture);
}

void sketch_app::rebuild_render_texture()
{
    if (canvas_texture.id == 0)
        return;
    ui::sync_texture(canvas_texture, surface.copy_pixels(), upload_buffer);
}

std::filesystem::path sketch_app::resolve_server_executable()
{
    return std::filesystem::current_path() / "server.exe";
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

void sketch_app::open_and_load()
    {
        if (const auto path = ui::open_binary_file_dialog(); path.has_value())
        {
            current_file = path->string();
            std::string load_status;
            if (!ui::load_binary_replay(path.value(), surface, load_status))
                current_file = "untitled";
            else
            {
                operation_log.reset();
                operation_log = std::make_unique<persistence_writer>(path->string());
            }
            set_status(std::move(load_status));
            active_stroke.reset();
            dirty.store(true);
        }
    }

void sketch_app::clear_canvas()
{
    active_stroke.reset();

    const uint32_t member = session_client ? session_client->member_id() : 1;
    const uint64_t operation_id = (static_cast<uint64_t>(member) << 32) |
                                  next_operation_number.fetch_add(1);
    const bool host_owns_canvas = !session_client || session_client->is_host();

    draw_operation clear_operation;
    clear_operation.operation_id = operation_id;
    clear_operation.member_id = member;
    clear_operation.tool = tool_type::clear;
    clear_operation.color = ui::BACKGROUND_COLOR;
    clear_operation.thickness = 1;

    if (host_owns_canvas)
    {
        clear_operation.seq = surface.apply(clear_operation);
        if (operation_log)
            operation_log->enqueue(clear_operation);
        rebuild_render_texture();
        dirty.store(true);
    }

    if (session_client && session_client->in_session())
    {
        if (!host_owns_canvas)
        {
            std::lock_guard lock(pending_mutex);
            pending_operations.insert(clear_operation.operation_id);
        }
        if (const auto sent = session_client->send_draw(clear_operation); !sent)
            set_status(sent.message);
        else
            set_status(host_owns_canvas ? "Canvas cleared" : "Clear requested");
    }
    else
        set_status("Canvas cleared");
}

void sketch_app::process_canvas_input(const canvas_input_state& input)
{
    const uint32_t member = session_client ? session_client->member_id() : 1;
    const uint64_t operation_id = (static_cast<uint64_t>(member) << 32) |
                                  next_operation_number.fetch_add(1);
    const bool host_owns_canvas = !session_client || session_client->is_host();
    const auto committed = ::process_canvas_input(
        surface, active_stroke, input, operation_id, member,
        active_color, active_thickness, host_owns_canvas, active_tool);
    if (!committed)
        return;

    if (host_owns_canvas && operation_log)
        operation_log->enqueue(*committed);
    if (host_owns_canvas && operation_log && !operation_log->healthy())
        set_status("Persistence write failed");
    dirty.store(true);
    if (session_client && session_client->in_session())
    {
        if (!host_owns_canvas)
        {
            std::lock_guard lock(pending_mutex);
            pending_operations.insert(committed->operation_id);
        }
        if (const auto sent = session_client->send_draw(*committed); !sent)
            set_status(sent.message);
    }
}

void sketch_app::poll_session()
    {
        while (!stop_poll.load())
        {
            if (!session_client)
                break;

            const auto msg_res = session_client->poll();
            if (!msg_res)
            {
                if (!stop_poll.load())
                    set_status(msg_res.message);
                break;
            }

            const auto& [header, payload] = msg_res.value;
            switch (header.opcode)
            {
            case Opcode::NOTIFICATION:
                {
                    if (!payload.empty())
                    {
                        switch (payload[0])
                        {
                        case notifcode::MEMBER_JOINED:
                            set_status("A member joined");
                            break;
                        case notifcode::MEMBER_LEFT:
                            set_status("A member left");
                            break;
                        case notifcode::SESSION_CLOSED:
                            session_client->mark_session_closed();
                            set_status("Session closed");
                            stop_poll.store(true);
                            break;
                        default:
                            break;
                        }
                    }
                    break;
                }
            case Opcode::DRAW:
                {
            const auto op_res = parseDrawOperation(payload);
                    if (!op_res)
                    {
                        set_status(op_res.message);
                        break;
                    }

                    draw_operation op = op_res.value;
                    {
                        std::lock_guard lock(pending_mutex);
                        pending_operations.erase(op.operation_id);
                    }
                    if (session_client->is_host())
                    {
                        if (const auto validation = validateDrawOperation(op, false); !validation)
                        {
                            set_status(validation.message);
                            break;
                        }
                        if (op.operation_id != 0 && surface.contains_operation(op.operation_id))
                            break;
                        op.seq = surface.apply(op);
                        if (operation_log)
                            operation_log->enqueue(op);
                        if (operation_log && !operation_log->healthy())
                            set_status("Persistence write failed");

                        if (const auto send_result = session_client->send_draw_raw(op); !send_result)
                            set_status(send_result.message);
                    }
                    else
                    {
                        if (op.seq == 0)
                        {
                            set_status("received uncommitted operation");
                            break;
                        }

                        const uint32_t expected = surface.next_sequence();
                        if (op.seq > expected)
                        {
                            if (const auto request = session_client->request_canvas_state(); !request)
                                set_status(request.message);
                            else
                                set_status("Canvas sync required");
                            break;
                        }
                        if (op.seq < expected)
                            break;

                        op.seq = surface.apply(op);
                    }

                    dirty.store(true);
                    break;
                }
            case Opcode::CANVAS_STATE:
                {
                    auto state_res = parseCanvasStateMessage(payload);
                    if (!state_res)
                    {
                        set_status(state_res.message);
                        break;
                    }

                    surface.load(state_res.value.operations);
                    dirty.store(true);
                    break;
                }
            case Opcode::CANVAS_STATE_REQUEST:
                {
                    if (session_client->is_host())
                    {
                        if (const auto send_result = session_client->send_canvas_state(surface.snapshot()); !send_result)
                            set_status(send_result.message);
                    }
                    break;
                }
            default:
                set_status("unexpected message opcode");
                break;
            }
        }
    }

result<bool> sketch_app::start_local_server()
    {
        if (local_server.running() || session_client)
            return {.value = false, .err = error::rejected, .message = "server already running"};

        const auto server_exe = resolve_server_executable();
        if (server_exe.empty() || !std::filesystem::exists(server_exe))
            return {.value = false, .err = error::rejected, .message = "server executable not found"};

        std::vector<std::string> args;
        args.emplace_back("--tcp-port");
        args.emplace_back("9000");
        args.emplace_back("--ws-port");
        args.emplace_back("0");

        auto process_result = local_server.start(server_process_launch{
            .executable = server_exe,
            .arguments = args
        });
        if (!process_result)
            return process_result;

        io_context = std::make_unique<net::io_context>();
        tcp_socket.emplace(tcp_addr{.host = "127.0.0.1", .port = "9000"}, *io_context);

        result connect_result{.value = false, .err = error::connect_failed, .message = "failed to connect to server"};
        for (int attempt = 0; attempt < 50; ++attempt)
        {
            connect_result = tcp_socket->connect();
            if (connect_result)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!connect_result)
        {
            if (tcp_socket && tcp_socket->is_open())
                tcp_socket->close();
            tcp_socket.reset();
            io_context.reset();
            local_server.stop(1);
            return connect_result;
        }

        session_client = std::make_unique<sessionClient>(*tcp_socket);

        if (const auto create_result = session_client->create("SketchSync"); !create_result)
        {
            if (tcp_socket && tcp_socket->is_open())
                tcp_socket->close();
            session_client.reset();
            tcp_socket.reset();
            io_context.reset();
            local_server.stop(1);
            return {.value = false, .err = create_result.err, .message = create_result.message};
        }

        stop_poll.store(false);
        poll_thread = std::thread(&sketch_app::poll_session, this);
        set_status("Local server started");
        dirty.store(true);
        return {.value = true, .err = error::none, .message = {}};
    }

void sketch_app::stop_local_server()
    {
        stop_poll.store(true);

        if (tcp_socket && tcp_socket->is_open())
            tcp_socket->close();

        if (poll_thread.joinable())
            poll_thread.join();

        session_client.reset();
        tcp_socket.reset();
        io_context.reset();
        local_server.stop();
        set_status("Local server stopped");
        dirty.store(true);
    }

    int sketch_app::run()
    {
        SetConfigFlags(FLAG_WINDOW_RESIZABLE);
        InitWindow(ui::WINDOW_WIDTH, ui::WINDOW_HEIGHT, "SketchSync");
        SetWindowMinSize(900, 600);
        const Image image = GenImageColor(static_cast<int>(surface.width), static_cast<int>(surface.height), WHITE);
        canvas_texture = LoadTextureFromImage(image);
        UnloadImage(image);
        rebuild_render_texture();
        SetTargetFPS(60);

        while (!WindowShouldClose())
        {
            const bool minimized = IsWindowMinimized();
            const float window_width = static_cast<float>(std::max(1, GetScreenWidth()));
            const float window_height = static_cast<float>(std::max(1, GetScreenHeight()));
            const float top_bar_height = std::clamp(window_height * 0.075f, 52.0f, 76.0f);
            const float drawable_width = window_width;
            const float drawable_height = std::max(1.0f, window_height - top_bar_height);
            const float max_canvas_width = drawable_width * 0.60f;
            const float max_canvas_height = drawable_height * 0.80f;
            const float canvas_aspect = static_cast<float>(surface.width) /
                                        static_cast<float>(std::max(1u, surface.height));
            float canvas_width = max_canvas_width;
            float canvas_height = canvas_width / canvas_aspect;
            if (canvas_height > max_canvas_height)
            {
                canvas_height = max_canvas_height;
                canvas_width = canvas_height * canvas_aspect;
            }
            const Rectangle canvas_rect{
                .x = (drawable_width - canvas_width) * 0.5f,
                .y = top_bar_height +
                     (drawable_height - canvas_height) * 0.5f,
                .width = canvas_width,
                .height = canvas_height
            };

            const Vector2 mouse = GetMousePosition();
            const float bar_padding = std::max(8.0f, window_width * 0.012f);
            const float button_gap = std::max(6.0f, window_width * 0.006f);
            const float button_height = top_bar_height - bar_padding * 2.0f;
            const float button_y = bar_padding;
            const float open_width = window_width * 0.085f;
            const float clear_width = window_width * 0.070f;
            const float start_width = window_width * 0.105f;
            const float stop_width = window_width * 0.085f;
            const Rectangle open_button{
                .x = bar_padding, .y = button_y, .width = open_width, .height = button_height};
            const Rectangle clear_button{
                .x = open_button.x + open_button.width + button_gap,
                .y = button_y, .width = clear_width, .height = button_height};
            const Rectangle start_button{
                .x = clear_button.x + clear_button.width + button_gap,
                .y = button_y, .width = start_width, .height = button_height};
            const Rectangle stop_button{
                .x = start_button.x + start_button.width + button_gap,
                .y = button_y, .width = stop_width, .height = button_height};

            constexpr std::array<std::pair<tool_type, const char*>, 9> tools{{
                {tool_type::freehand, "Freehand"},
                {tool_type::brush, "Brush"},
                {tool_type::line, "Line"},
                {tool_type::rect, "Rect"},
                {tool_type::filled_rect, "Fill Rect"},
                {tool_type::ellipse, "Ellipse"},
                {tool_type::filled_ellipse, "Fill Ellipse"},
                {tool_type::eraser, "Eraser"},
                {tool_type::bucket_fill, "Bucket Fill"}
            }};
            const float tool_panel_x = std::max(8.0f, canvas_rect.x * 0.12f);
            const float tool_width = std::max(90.0f, canvas_rect.x * 0.72f);
            const float tool_height = std::max(20.0f, std::min(30.0f, canvas_rect.height / 13.0f));
            const float tool_gap = std::max(3.0f, tool_height * 0.10f);

            // Right panel geometry
            const float right_gap = std::max(8.0f, window_width * 0.006f);
            const float right_panel_x = canvas_rect.x + canvas_rect.width + right_gap;
            const float right_panel_width = std::max(100.0f, window_width - right_panel_x - bar_padding);
            const float right_panel_y = canvas_rect.y;

            constexpr std::array<uint8_t, 6> thicknesses{1, 2, 4, 8, 16, 24};
            const float thick_btn_w = (right_panel_width - button_gap) * 0.5f;
            const float thick_btn_h = std::max(22.0f, thick_btn_w * 0.45f);
            const float right_thick_start_y = right_panel_y + 18.0f;
            const float right_color_label_y = right_thick_start_y + 3.0f * (thick_btn_h + button_gap) + 34.0f;
            const float right_color_start_y = right_color_label_y + 18.0f + button_gap;

            constexpr std::array<uint32_t, 16> palette{
                0xFF000000, 0xFF505050, 0xFFAAAAAA, 0xFFFFFFFF,
                0xFFE53935, 0xFFFF7043, 0xFFFFB300, 0xFF43A047,
                0xFF00ACC1, 0xFF1E88E5, 0xFF5E35B1, 0xFFE91E63,
                0xFF795548, 0xFF558B2F, 0xFFFF80AB, 0xFF37474F,
            };
            constexpr int color_cols = 4;
            const float color_swatch = (right_panel_width - static_cast<float>(color_cols - 1) * button_gap)
                                       / static_cast<float>(color_cols);

            if (ui::button_hit(open_button))
                open_and_load();

            if (ui::button_hit(clear_button))
                clear_canvas();

            if (ui::button_hit(start_button))
            {
                if (const auto result = start_local_server(); !result)
                    set_status(result.message);
            }

            if (ui::button_hit(stop_button))
                stop_local_server();

            for (size_t i = 0; i < tools.size(); ++i)
            {
                const Rectangle tool_button{
                    .x = tool_panel_x,
                    .y = canvas_rect.y + static_cast<float>(i) * (tool_height + tool_gap),
                    .width = tool_width,
                    .height = tool_height};
                if (ui::button_hit(tool_button))
                {
                    active_tool = tools[i].first;
                    active_stroke.reset();
                    set_status(std::string("Tool: ") + tools[i].second);
                }
            }
            for (size_t i = 0; i < palette.size(); ++i)
            {
                const Rectangle swatch{
                    .x = right_panel_x + static_cast<float>(i % color_cols) * (color_swatch + button_gap),
                    .y = right_color_start_y + static_cast<float>(i / color_cols) * (color_swatch + button_gap),
                    .width = color_swatch,
                    .height = color_swatch};
                if (ui::button_hit(swatch))
                    active_color = palette[i];
            }
            for (size_t i = 0; i < thicknesses.size(); ++i)
            {
                const Rectangle thick_btn{
                    .x = right_panel_x + static_cast<float>(i % 2) * (thick_btn_w + button_gap),
                    .y = right_thick_start_y + static_cast<float>(i / 2) * (thick_btn_h + button_gap),
                    .width = thick_btn_w,
                    .height = thick_btn_h};
                if (ui::button_hit(thick_btn))
                    active_thickness = thicknesses[i];
            }

            if (IsKeyPressed(KEY_O) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)))
                open_and_load();

            if (!minimized)
                process_canvas_input(canvas_input_state{
                    .inside = CheckCollisionPointRec(mouse, canvas_rect),
                    .pressed = IsMouseButtonPressed(MOUSE_LEFT_BUTTON),
                    .down = IsMouseButtonDown(MOUSE_LEFT_BUTTON),
                    .released = IsMouseButtonReleased(MOUSE_LEFT_BUTTON),
                    .position = ui::normalize_point(ui::clamp_to_canvas(mouse, canvas_rect), canvas_rect),
                    .tool = active_tool
                });

            if (dirty.load())
            {
                rebuild_render_texture();
                dirty.store(false);
            }

            if (active_stroke.has_value())
                ui::sync_texture(canvas_texture,
                                 surface.copy_pixels_with_preview(*active_stroke), upload_buffer);

            BeginDrawing();
            ClearBackground(Color{.r = 240, .g = 242, .b = 246, .a = 255});

            DrawRectangle(0, 0, static_cast<int>(window_width), static_cast<int>(top_bar_height),
                          Color{.r = 248, .g = 249, .b = 252, .a = 255});
            DrawLine(0, static_cast<int>(top_bar_height - 1.0f), static_cast<int>(window_width),
                     static_cast<int>(top_bar_height - 1.0f),
                     Color{.r = 210, .g = 215, .b = 224, .a = 255});

            ui::draw_button(open_button, "Open File");
            ui::draw_button(clear_button, "Clear");
            ui::draw_button(start_button, "Start Server");
            ui::draw_button(stop_button, "Stop Server");
            for (size_t i = 0; i < tools.size(); ++i)
            {
                const Rectangle tool_button{
                    .x = tool_panel_x,
                    .y = canvas_rect.y + static_cast<float>(i) * (tool_height + tool_gap),
                    .width = tool_width,
                    .height = tool_height};
                ui::draw_button(tool_button, tools[i].second, tools[i].first == active_tool);
            }
            // Right panel: size chooser
            DrawText("Size", static_cast<int>(right_panel_x), static_cast<int>(right_panel_y), 13,
                     Color{.r = 72, .g = 76, .b = 88, .a = 255});
            for (size_t i = 0; i < thicknesses.size(); ++i)
            {
                const Rectangle thick_btn{
                    .x = right_panel_x + static_cast<float>(i % 2) * (thick_btn_w + button_gap),
                    .y = right_thick_start_y + static_cast<float>(i / 2) * (thick_btn_h + button_gap),
                    .width = thick_btn_w,
                    .height = thick_btn_h};
                const std::string thick_lbl = std::to_string(thicknesses[i]);
                ui::draw_button(thick_btn, thick_lbl.c_str(), thicknesses[i] == active_thickness);
            }
            // Size preview: circle at current thickness filled with active color
            {
                const float preview_cy = right_thick_start_y + 3.0f * (thick_btn_h + button_gap) + 16.0f;
                const int pr = std::clamp(static_cast<int>(active_thickness), 1, static_cast<int>(right_panel_width * 0.4f));
                DrawCircle(static_cast<int>(right_panel_x + right_panel_width * 0.5f),
                           static_cast<int>(preview_cy), static_cast<float>(pr),
                           ui::argb_to_color(active_color));
                DrawCircleLines(static_cast<int>(right_panel_x + right_panel_width * 0.5f),
                                static_cast<int>(preview_cy), static_cast<float>(pr),
                                Color{.r = 120, .g = 126, .b = 138, .a = 255});
            }
            // Right panel: color chooser
            DrawText("Color", static_cast<int>(right_panel_x), static_cast<int>(right_color_label_y), 13,
                     Color{.r = 72, .g = 76, .b = 88, .a = 255});
            for (size_t i = 0; i < palette.size(); ++i)
            {
                const Rectangle swatch{
                    .x = right_panel_x + static_cast<float>(i % color_cols) * (color_swatch + button_gap),
                    .y = right_color_start_y + static_cast<float>(i / color_cols) * (color_swatch + button_gap),
                    .width = color_swatch,
                    .height = color_swatch};
                DrawRectangleRec(swatch, ui::argb_to_color(palette[i]));
                DrawRectangleLinesEx(swatch, palette[i] == active_color ? 3.0f : 1.0f,
                                     palette[i] == active_color ? Color{.r = 30, .g = 30, .b = 30, .a = 255}
                                                                 : Color{.r = 150, .g = 150, .b = 150, .a = 255});
            }
            const int title_size = std::max(16, static_cast<int>(top_bar_height * 0.34f));
            const int detail_size = std::max(11, static_cast<int>(top_bar_height * 0.22f));
            const int title_x = static_cast<int>(window_width * 0.43f);
            DrawText("SketchSync", title_x, static_cast<int>(top_bar_height * 0.12f), title_size,
                     Color{.r = 42, .g = 46, .b = 58, .a = 255});
            DrawText(current_file.c_str(), title_x, static_cast<int>(top_bar_height * 0.58f), detail_size,
                     Color{.r = 92, .g = 96, .b = 108, .a = 255});
            const auto status_text = get_status();
            DrawText(status_text.c_str(), static_cast<int>(window_width * 0.70f),
                     static_cast<int>(top_bar_height * 0.38f), detail_size,
                     Color{.r = 92, .g = 96, .b = 108, .a = 255});

            DrawTexturePro(canvas_texture,
                           Rectangle{.x = 0.0f,
                                     .y = 0.0f,
                                     .width = static_cast<float>(canvas_texture.width),
                                     .height = static_cast<float>(canvas_texture.height)},
                           canvas_rect,
                           Vector2{.x = 0.0f, .y = 0.0f},
                           0.0f,
                           WHITE);
            DrawRectangleLines(static_cast<int>(canvas_rect.x),
                               static_cast<int>(canvas_rect.y),
                               static_cast<int>(canvas_rect.width),
                               static_cast<int>(canvas_rect.height),
                               Color{.r = 120, .g = 126, .b = 138, .a = 255});

            EndDrawing();
        }

    stop_local_server();
    ui::close_window();
    return 0;
}
