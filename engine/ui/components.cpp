#include "components.h"
#include "engine/ui/ui.h"

namespace ui {

    // Helper for proper text rendering using the app font
    static void DrawTextLayout(const char* text, const int x, const int y, const float size, const Color color) {
        if (IsFontValid(default_font)) {
            DrawTextEx(default_font, text, { .x = static_cast<float>(x), .y = static_cast<float>(y) }, size, 0.0f, color);
        } else {
            DrawText(text, x, y, static_cast<int>(size), color);
        }
    }

    void Button::draw() const {
        const Vector2 mouse = GetMousePosition();
        const bool hovered = CheckCollisionPointRec(mouse, bounds);

        Color fill;
        if (active) fill = {.r = 52, .g = 120, .b = 220, .a = 255};
        else if (is_down && hovered) fill = {.r = 82, .g = 140, .b = 240, .a = 255};
        else if (hovered) fill = {.r = 98, .g = 102, .b = 114, .a = 255};
        else fill = {.r = 72, .g = 76, .b = 88, .a = 255};

        DrawRectangleRec(bounds, fill);
        DrawRectangleLinesEx(bounds, active ? 2.5f : 1.5f, {.r = 165, .g = 170, .b = 184, .a = 255});

        constexpr float text_size = 18.0f;
        const Vector2 label_size = IsFontValid(default_font) ? MeasureTextEx(default_font, label.c_str(), text_size, 1.0f) : Vector2{ .x = static_cast<float>(MeasureText(label.c_str(), static_cast<int>(text_size))), .y = text_size };

        DrawTextLayout(label.c_str(),
                 static_cast<int>(bounds.x + (bounds.width - label_size.x) * 0.5f),
                 static_cast<int>(bounds.y + (bounds.height - label_size.y) * 0.5f),
                 text_size, RAYWHITE);
    }

    bool Button::update() {
        const Vector2 mouse = GetMousePosition();
        const bool hovered = CheckCollisionPointRec(mouse, bounds);

        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && hovered) {
            is_down = true;
        }

        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
            const bool clicked = is_down && hovered;
            is_down = false;
            return clicked;
        }

        return false;
    }

    void TextField::draw() const {
        DrawRectangleRec(bounds, WHITE);
        DrawRectangleLinesEx(bounds, focused ? 2.0f : 1.0f, focused ? BLUE : GRAY);

        const char* text = value ? value->c_str() : "";
        DrawTextLayout(text, static_cast<int>(bounds.x) + 8, static_cast<int>(bounds.y) + static_cast<int>(bounds.height - 14) / 2, 14.0f, BLACK);

        if (!label.empty() && (!value || value->empty())) {
            DrawTextLayout(label.c_str(), static_cast<int>(bounds.x) + 8, static_cast<int>(bounds.y) + static_cast<int>(bounds.height - 14) / 2, 14.0f, LIGHTGRAY);
        }
    }

    void TextField::update() {
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            focused = CheckCollisionPointRec(GetMousePosition(), bounds);
        }

        if (focused && value) {
            int key = GetCharPressed();
            while (key > 0) {
                if (key >= 32 && key <= 125 && value->size() < 32) {
                    value->push_back(static_cast<char>(key));
                }
                key = GetCharPressed();
            }
            if (IsKeyPressed(KEY_BACKSPACE) && !value->empty()) {
                value->pop_back();
            }
        }
    }

    void ToolButton::draw() const {
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
        const Color fill = selected ? (Color){.r = 52, .g = 120, .b = 220, .a = 255} : (hovered ? (Color){.r = 98, .g = 102, .b = 114, .a = 255} : (Color){.r = 72, .g = 76, .b = 88, .a = 255});

        DrawRectangleRec(bounds, fill);
        DrawRectangleLinesEx(bounds, 1.5f, {.r = 165, .g = 170, .b = 184, .a = 255});
        DrawTextLayout(label.c_str(), static_cast<int>(bounds.x) + 10, static_cast<int>(bounds.y) + static_cast<int>(bounds.height - 14) / 2, 14.0f, RAYWHITE);
    }

    bool ToolButton::update() {
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && hovered) is_down = true;
        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
            const bool clicked = is_down && hovered;
            is_down = false;
            return clicked;
        }
        return false;
    }

    void ColorSwatch::draw() const {
        DrawRectangleRec(bounds, ui::argb_to_color(color));
        DrawRectangleLinesEx(bounds, selected ? 3.0f : 1.5f, selected ? BLACK : DARKGRAY);
    }

    bool ColorSwatch::update() const
    {
        if (CheckCollisionPointRec(GetMousePosition(), bounds) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            return true;
        }
        return false;
    }

    void Panel::draw() const {
        DrawRectangleRec(bounds, bg_color);
        DrawLineEx({.x = bounds.x, .y = bounds.y}, {.x = bounds.x + bounds.width, .y = bounds.y}, 1.5f, GRAY);
    }
}
