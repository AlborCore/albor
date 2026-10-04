#pragma once

#include <cstdint>

struct Component462;

static constexpr int kProp463_Color = 463 * 4 + 0;
static constexpr int kProp463_Opacity = 463 * 4 + 1;
static constexpr int kProp463_Width = 463 * 4 + 2;
static constexpr int kProp463_Height = 463 * 4 + 3;

template <typename Derived>
struct Component463
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component462* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component462* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
