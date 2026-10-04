#pragma once

#include <cstdint>

struct Component301;

static constexpr int kProp302_Color = 302 * 4 + 0;
static constexpr int kProp302_Opacity = 302 * 4 + 1;
static constexpr int kProp302_Width = 302 * 4 + 2;
static constexpr int kProp302_Height = 302 * 4 + 3;

template <typename Derived>
struct Component302
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component301* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component301* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
