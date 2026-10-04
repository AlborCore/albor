#pragma once

#include <cstdint>

struct Component492;

static constexpr int kProp493_Color = 493 * 4 + 0;
static constexpr int kProp493_Opacity = 493 * 4 + 1;
static constexpr int kProp493_Width = 493 * 4 + 2;
static constexpr int kProp493_Height = 493 * 4 + 3;

template <typename Derived>
struct Component493
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component492* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component492* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
