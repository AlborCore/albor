#pragma once

#include <cstdint>

struct Component344;

static constexpr int kProp345_Color = 345 * 4 + 0;
static constexpr int kProp345_Opacity = 345 * 4 + 1;
static constexpr int kProp345_Width = 345 * 4 + 2;
static constexpr int kProp345_Height = 345 * 4 + 3;

template <typename Derived>
struct Component345
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component344* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component344* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
