#pragma once

#include <cstdint>

struct Component195;

static constexpr int kProp196_Color = 196 * 4 + 0;
static constexpr int kProp196_Opacity = 196 * 4 + 1;
static constexpr int kProp196_Width = 196 * 4 + 2;
static constexpr int kProp196_Height = 196 * 4 + 3;

template <typename Derived>
struct Component196
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component195* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component195* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
