#include "app_layout.h"
#include "engine/ui/ui.h"
#include <array>

namespace ui {

    // Internal helper for proper text rendering using the app font
    static void DrawTextLayout(const char* text, const int x, const int y, const float size, const Color color) {
        if (IsFontValid(default_font)) {
            DrawTextEx(default_font, text, { static_cast<float>(x), static_cast<float>(y) }, size, 0.0f, color);
        } else {
            DrawText(text, x, y, static_cast<int>(size), color);
        }
    }

    static void DrawTextWrapped(const std::string& text, const int x, int y, const float max_width, const float font_size, const Color color) {
        if (text.empty()) return;
        
        std::string current_line;
        size_t start = 0;
        const float line_height = font_size + 4.0f;
        
        while (start < text.size()) {
            const size_t next_space = text.find(' ', start);
            std::string word;
            if (next_space == std::string::npos) {
                word = text.substr(start);
                start = text.size();
            } else {
                word = text.substr(start, next_space - start + 1);
                start = next_space + 1;
            }
            
            const std::string test_line = current_line + word;
            float text_width = 0.0f;
            if (IsFontValid(default_font)) {
                text_width = MeasureTextEx(default_font, test_line.c_str(), font_size, 0.0f).x;
            } else {
                text_width = static_cast<float>(MeasureText(test_line.c_str(), static_cast<int>(font_size)));
            }
            
            if (text_width > max_width && !current_line.empty()) {
                DrawTextLayout(current_line.c_str(), x, y, font_size, color);
                y += static_cast<int>(line_height);
                current_line = word;
            } else {
                current_line = test_line;
            }
        }
        
        if (!current_line.empty()) {
            DrawTextLayout(current_line.c_str(), x, y, font_size, color);
        }
    }

    void AppLayout::update_layout(const float window_width, const float window_height, const connection_protocol protocol) {
        left_panel.bounds = {.x = 0, .y = top_bar_height, .width = left_panel_width, .height = window_height - top_bar_height};
        right_panel.bounds = {.x = window_width - right_panel_width, .y = top_bar_height, .width = right_panel_width, .height = window_height - top_bar_height};
        bottom_panel.bounds = {.x = 0, .y = window_height - bottom_panel_height, .width = window_width, .height = bottom_panel_height};

        open_btn.bounds = {.x = 15, .y = 12, .width = 90, .height = 30}; open_btn.label = "Open";
        clear_btn.bounds = {.x = 115, .y = 12, .width = 90, .height = 30}; clear_btn.label = "Clear";

        if (protocol == connection_protocol::websocket) {
            host_field.bounds = {.x = 15, .y = window_height - 120, .width = 200, .height = 30}; host_field.label = "Server Address";
            port_field.bounds = {.x = 15, .y = window_height - 65, .width = 200, .height = 30}; port_field.label = "Port";

            protocol_toggle.bounds = {.x = 230, .y = window_height - 120, .width = 90, .height = 30};
            connect_btn.bounds = {.x = 230, .y = window_height - 70, .width = 110, .height = 35};
            local_server_btn.bounds = {.x = 350, .y = window_height - 70, .width = 150, .height = 35};
        } else {
            protocol_toggle.bounds = {.x = 15, .y = window_height - 120, .width = 90, .height = 30};
            connect_btn.bounds = {.x = 15, .y = window_height - 70, .width = 110, .height = 35};
            local_server_btn.bounds = {.x = 135, .y = window_height - 70, .width = 150, .height = 35};
        }

        session_id_field.bounds = {.x = window_width - 215, .y = window_height - 120, .width = 200, .height = 30}; session_id_field.label = "Session ID";
        join_btn.bounds = {.x = window_width - 385, .y = window_height - 70, .width = 80, .height = 35}; join_btn.label = "Join";
        create_btn.bounds = {.x = window_width - 295, .y = window_height - 70, .width = 80, .height = 35}; create_btn.label = "Create";
        leave_btn.bounds = {.x = window_width - 295, .y = window_height - 70, .width = 80, .height = 35}; leave_btn.label = "Leave";

        constexpr std::array<std::pair<tool_type, const char*>, 9> tools{{{tool_type::freehand, "Freehand"}, {tool_type::brush, "Brush"}, {tool_type::line, "Line"}, {tool_type::rect, "Rect"}, {tool_type::filled_rect, "Fill Rect"}, {tool_type::ellipse, "Ellipse"}, {tool_type::filled_ellipse, "Fill Ellipse"}, {tool_type::eraser, "Eraser"}, {tool_type::bucket_fill, "Bucket Fill"}}};
        if (tool_buttons.empty()) {
            for (const auto& [fst, snd] : tools) { ToolButton tb; tb.label = snd; tb.tool_id = static_cast<int>(fst); tool_buttons.push_back(tb); }
        }
        for (size_t i = 0; i < tool_buttons.size(); ++i) tool_buttons[i].bounds = {.x = window_width - right_panel_width + 15, .y = top_bar_height + 45 + static_cast<float>(i) * 35, .width = right_panel_width - 30, .height = 30};

        if (thickness_buttons.empty()) {
            for (constexpr std::array<uint8_t, 4> thicknesses{1, 2, 4, 8}; const auto t : thicknesses) { Button b; b.label = std::to_string(t); thickness_buttons.push_back(b); }
        }
        float next_y = top_bar_height + 55 + static_cast<float>(tool_buttons.size()) * 35;
        for (size_t i = 0; i < thickness_buttons.size(); ++i) thickness_buttons[i].bounds = {.x = window_width - right_panel_width + 15 + static_cast<float>(i) * 45, .y = next_y + 25, .width = 40, .height = 30};

        if (color_swatches.empty()) {
            for (constexpr std::array palette{0xFF000000, 0xFFE53935, 0xFFFFB300, 0xFF43A047, 0xFF1E88E5, 0xFF5E35B1, 0xFF795548, 0xFFFFFFFF}; const auto c : palette) { ColorSwatch cs; cs.color = c; color_swatches.push_back(cs); }
        }
        next_y += 75;
        for (size_t i = 0; i < color_swatches.size(); ++i) color_swatches[i].bounds = {.x = window_width - right_panel_width + 15 + static_cast<float>(i % 4) * 45, .y = next_y + 25 + static_cast<float>(i / 4) * 45, .width = 40, .height = 40};
    }

    void AppLayout::draw(const network_session_state& net, const std::string& status, const std::string& current_file, const bool server_running) const {
        left_panel.draw();
        right_panel.draw();
        bottom_panel.draw();

        DrawRectangle(0, 0, GetScreenWidth(), static_cast<int>(top_bar_height), LIGHTGRAY);
        DrawTextLayout("SketchSync", GetScreenWidth()/2 - 60, 12, 22.0f, DARKGRAY);
        DrawTextLayout(current_file.c_str(), GetScreenWidth()/2 - 30, 34, 11.0f, GRAY);

        open_btn.draw();
        clear_btn.draw();

        // Left Info
        DrawTextLayout("Network Info", 15, static_cast<int>(top_bar_height) + 15, 18.0f, DARKGRAY);
        if (net.net.protocol == connection_protocol::tcp) {
            if (net.net.connected) {
                DrawTextLayout(TextFormat("TCP: %s:%s", net.net.host.c_str(), net.net.port.c_str()), 15, static_cast<int>(top_bar_height) + 45, 14.0f, GRAY);
            } else {
                DrawTextLayout("TCP: UDP Discovery", 15, static_cast<int>(top_bar_height) + 45, 14.0f, GRAY);
            }
        } else {
            DrawTextLayout(TextFormat("WS: %s:%s", net.net.host.c_str(), net.net.port.c_str()), 15, static_cast<int>(top_bar_height) + 45, 14.0f, GRAY);
        }

        const char* s_str = net.net.connected ? "Connected" : (net.state == connection_state::connecting ? "Connecting..." : "Disconnected");
        const Color s_col = net.net.connected ? GREEN : (net.state == connection_state::connecting ? ORANGE : RED);
        DrawTextLayout(TextFormat("Status: %s", s_str), 15, static_cast<int>(top_bar_height) + 65, 14.0f, s_col);
        if (net.session.in_session) DrawTextLayout(TextFormat("Session: #%d (M:%d)", net.session.session_id, net.session.member_id), 15, static_cast<int>(top_bar_height) + 110, 14.0f, BLUE);
        else DrawTextLayout("Not in session", 15, static_cast<int>(top_bar_height) + 110, 14.0f, GRAY);
        DrawTextWrapped(status, 15, static_cast<int>(top_bar_height) + 145, left_panel_width - 30.0f, 13.0f, DARKBLUE);

        // Right Tools
        DrawTextLayout("Tools", GetScreenWidth() - static_cast<int>(right_panel_width) + 15, static_cast<int>(top_bar_height) + 15, 18.0f, DARKGRAY);
        for (const auto& tb : tool_buttons) tb.draw();
        float next_y = top_bar_height + 55 + static_cast<float>(tool_buttons.size()) * 35;
        DrawTextLayout("Thickness", GetScreenWidth() - static_cast<int>(right_panel_width) + 15, static_cast<int>(next_y), 16.0f, DARKGRAY);
        for (const auto& b : thickness_buttons) b.draw();
        next_y += 75;
        DrawTextLayout("Colors", GetScreenWidth() - static_cast<int>(right_panel_width) + 15, static_cast<int>(next_y), 16.0f, DARKGRAY);
        for (const auto& cs : color_swatches) cs.draw();

        // Controls
        if (net.net.protocol == connection_protocol::websocket) {
            host_field.draw();
            port_field.draw();
        }

        Button proto = protocol_toggle;
        proto.label = (net.net.protocol == connection_protocol::tcp) ? "TCP" : "WS";
        proto.draw();

        Button conn = connect_btn;
        conn.label = net.net.connected ? "Disconnect" : "Connect";
        conn.draw();

        Button local = local_server_btn;
        local.label = server_running ? "Stop Local" : "Start Local";
        local.draw();

        session_id_field.draw();
        if (!net.session.in_session) { join_btn.draw(); create_btn.draw(); }
        else { leave_btn.draw(); }
    }
}
