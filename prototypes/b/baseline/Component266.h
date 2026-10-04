#pragma once

#include <cstdint>

struct Component265;

static constexpr int kProp266_Color = 266 * 4 + 0;
static constexpr int kProp266_Opacity = 266 * 4 + 1;
static constexpr int kProp266_Width = 266 * 4 + 2;
static constexpr int kProp266_Height = 266 * 4 + 3;

template <typename Derived>
struct Component266
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component265* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component265* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
