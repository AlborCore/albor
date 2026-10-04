#pragma once

#include <cstdint>

struct Component203;

static constexpr int kProp204_Color = 204 * 4 + 0;
static constexpr int kProp204_Opacity = 204 * 4 + 1;
static constexpr int kProp204_Width = 204 * 4 + 2;
static constexpr int kProp204_Height = 204 * 4 + 3;

template <typename Derived>
struct Component204
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component203* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component203* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
