#pragma once

#include <cstdint>

struct Component403;

static constexpr int kProp404_Color = 404 * 4 + 0;
static constexpr int kProp404_Opacity = 404 * 4 + 1;
static constexpr int kProp404_Width = 404 * 4 + 2;
static constexpr int kProp404_Height = 404 * 4 + 3;

template <typename Derived>
struct Component404
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component403* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component403* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
