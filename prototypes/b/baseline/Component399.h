#pragma once

#include <cstdint>

struct Component398;

static constexpr int kProp399_Color = 399 * 4 + 0;
static constexpr int kProp399_Opacity = 399 * 4 + 1;
static constexpr int kProp399_Width = 399 * 4 + 2;
static constexpr int kProp399_Height = 399 * 4 + 3;

template <typename Derived>
struct Component399
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component398* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component398* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
