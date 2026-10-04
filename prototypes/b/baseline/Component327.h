#pragma once

#include <cstdint>

struct Component326;

static constexpr int kProp327_Color = 327 * 4 + 0;
static constexpr int kProp327_Opacity = 327 * 4 + 1;
static constexpr int kProp327_Width = 327 * 4 + 2;
static constexpr int kProp327_Height = 327 * 4 + 3;

template <typename Derived>
struct Component327
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component326* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component326* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
