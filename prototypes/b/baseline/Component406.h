#pragma once

#include <cstdint>

struct Component405;

static constexpr int kProp406_Color = 406 * 4 + 0;
static constexpr int kProp406_Opacity = 406 * 4 + 1;
static constexpr int kProp406_Width = 406 * 4 + 2;
static constexpr int kProp406_Height = 406 * 4 + 3;

template <typename Derived>
struct Component406
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component405* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component405* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
