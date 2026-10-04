#pragma once

#include <cstdint>

struct Component253;

static constexpr int kProp254_Color = 254 * 4 + 0;
static constexpr int kProp254_Opacity = 254 * 4 + 1;
static constexpr int kProp254_Width = 254 * 4 + 2;
static constexpr int kProp254_Height = 254 * 4 + 3;

template <typename Derived>
struct Component254
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component253* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component253* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
