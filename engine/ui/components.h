#ifndef SKETCHSYNC_UI_COMPONENTS_H
#define SKETCHSYNC_UI_COMPONENTS_H

#include "raylib.h"
#include <string>
#include <cstdint>

namespace ui {

    struct Button {
        Rectangle bounds;
        std::string label;
        bool active = false;
        bool is_down = false; // Tracks if the mouse was pressed ON this button

        void draw() const;
        bool update(); // Returns true if a full click cycle completed
    };

    struct TextField {
        Rectangle bounds;
        std::string label;
        std::string* value = nullptr; // Binds directly to the data string
        bool focused = false;

        void draw() const;
        void update();

        void set_value(std::string* v) { value = v; }
        [[nodiscard]] std::string get_text() const { return value ? *value : ""; }
        void set_text(const std::string& t) const { if (value) *value = t; }
    };

    struct ToolButton {
        Rectangle bounds;
        std::string label;
        int tool_id = 0; // Stores the enum value
        bool selected = false;
        bool is_down = false;

        void draw() const;
        bool update();
    };

    struct ColorSwatch {
        Rectangle bounds{};
        uint32_t color{};
        bool selected = false;

        void draw() const;
        [[nodiscard]] bool update() const; // Returns true if clicked
    };

    // Higher-level layout containers
    struct Panel {
        Rectangle bounds{};
        Color bg_color = {.r = 245, .g = 245, .b = 245, .a = 255};
        void draw() const;
    };
}

#endif
