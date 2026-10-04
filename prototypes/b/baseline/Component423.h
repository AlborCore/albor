#pragma once

#include <cstdint>

struct Component422;

static constexpr int kProp423_Color = 423 * 4 + 0;
static constexpr int kProp423_Opacity = 423 * 4 + 1;
static constexpr int kProp423_Width = 423 * 4 + 2;
static constexpr int kProp423_Height = 423 * 4 + 3;

template <typename Derived>
struct Component423
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component422* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component422* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
