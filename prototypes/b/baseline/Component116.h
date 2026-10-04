#pragma once

#include <cstdint>

struct Component115;

static constexpr int kProp116_Color = 116 * 4 + 0;
static constexpr int kProp116_Opacity = 116 * 4 + 1;
static constexpr int kProp116_Width = 116 * 4 + 2;
static constexpr int kProp116_Height = 116 * 4 + 3;

template <typename Derived>
struct Component116
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component115* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component115* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
