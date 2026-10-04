#pragma once

#include <cstdint>

struct Component149;

static constexpr int kProp150_Color = 150 * 4 + 0;
static constexpr int kProp150_Opacity = 150 * 4 + 1;
static constexpr int kProp150_Width = 150 * 4 + 2;
static constexpr int kProp150_Height = 150 * 4 + 3;

template <typename Derived>
struct Component150
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component149* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component149* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
