#pragma once

#include <cstdint>

struct Component423;

static constexpr int kProp424_Color = 424 * 4 + 0;
static constexpr int kProp424_Opacity = 424 * 4 + 1;
static constexpr int kProp424_Width = 424 * 4 + 2;
static constexpr int kProp424_Height = 424 * 4 + 3;

template <typename Derived>
struct Component424
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component423* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component423* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
