#pragma once

#include <cstdint>

struct Component461;

static constexpr int kProp462_Color = 462 * 4 + 0;
static constexpr int kProp462_Opacity = 462 * 4 + 1;
static constexpr int kProp462_Width = 462 * 4 + 2;
static constexpr int kProp462_Height = 462 * 4 + 3;

template <typename Derived>
struct Component462
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component461* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component461* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
