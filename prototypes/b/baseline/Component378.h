#pragma once

#include <cstdint>

struct Component377;

static constexpr int kProp378_Color = 378 * 4 + 0;
static constexpr int kProp378_Opacity = 378 * 4 + 1;
static constexpr int kProp378_Width = 378 * 4 + 2;
static constexpr int kProp378_Height = 378 * 4 + 3;

template <typename Derived>
struct Component378
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component377* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component377* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
