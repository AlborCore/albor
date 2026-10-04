#pragma once

#include <cstdint>

struct Component353;

static constexpr int kProp354_Color = 354 * 4 + 0;
static constexpr int kProp354_Opacity = 354 * 4 + 1;
static constexpr int kProp354_Width = 354 * 4 + 2;
static constexpr int kProp354_Height = 354 * 4 + 3;

template <typename Derived>
struct Component354
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component353* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component353* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
