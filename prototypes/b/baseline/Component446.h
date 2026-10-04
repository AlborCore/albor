#pragma once

#include <cstdint>

struct Component445;

static constexpr int kProp446_Color = 446 * 4 + 0;
static constexpr int kProp446_Opacity = 446 * 4 + 1;
static constexpr int kProp446_Width = 446 * 4 + 2;
static constexpr int kProp446_Height = 446 * 4 + 3;

template <typename Derived>
struct Component446
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component445* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component445* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
