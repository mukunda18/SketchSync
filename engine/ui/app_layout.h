#ifndef SKETCHSYNC_APP_LAYOUT_H
#define SKETCHSYNC_APP_LAYOUT_H

#include "components.h"
#include "engine/client/network_session_state.h"
#include "common/canvas/draw_operation.h"
#include <string>

namespace ui {

    struct AppLayout {
        // Layout Config
        float left_panel_width = 220.0f;
        float right_panel_width = 220.0f;
        float bottom_panel_height = 160.0f;
        float top_bar_height = 50.0f;

        // Components
        Panel left_panel;
        Panel right_panel;
        Panel bottom_panel;

        Button open_btn;
        Button save_btn;
        Button save_as_btn;
        Button auto_save_btn;
        Button clear_btn;

        // Network Controls
        TextField host_field;
        TextField port_field;
        Button protocol_toggle;
        Button connect_btn;
        Button local_server_btn;

        // Session Controls
        TextField session_id_field;
        Button join_btn;
        Button create_btn;
        Button leave_btn;

        // Tools & Palette
        std::vector<ToolButton> tool_buttons;
        std::vector<ColorSwatch> color_swatches;
        std::vector<Button> thickness_buttons;

        void update_layout(float window_width, float window_height, connection_protocol protocol = connection_protocol::tcp);
        void draw(const network_session_state& net, const std::string& status, const std::string& current_file, bool server_running, bool auto_save_on) const;
    };
}

#endif
