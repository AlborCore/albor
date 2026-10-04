#pragma once

#include <cstdint>

struct Component310;

static constexpr int kProp311_Color = 311 * 4 + 0;
static constexpr int kProp311_Opacity = 311 * 4 + 1;
static constexpr int kProp311_Width = 311 * 4 + 2;
static constexpr int kProp311_Height = 311 * 4 + 3;

template <typename Derived>
struct Component311
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component310* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component310* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
