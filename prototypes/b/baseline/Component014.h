#pragma once

#include <cstdint>

struct Component013;

static constexpr int kProp014_Color = 14 * 4 + 0;
static constexpr int kProp014_Opacity = 14 * 4 + 1;
static constexpr int kProp014_Width = 14 * 4 + 2;
static constexpr int kProp014_Height = 14 * 4 + 3;

template <typename Derived>
struct Component014
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component013* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component013* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
