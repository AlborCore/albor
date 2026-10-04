#pragma once

#include <cstdint>

struct Component157;

static constexpr int kProp158_Color = 158 * 4 + 0;
static constexpr int kProp158_Opacity = 158 * 4 + 1;
static constexpr int kProp158_Width = 158 * 4 + 2;
static constexpr int kProp158_Height = 158 * 4 + 3;

template <typename Derived>
struct Component158
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component157* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component157* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
