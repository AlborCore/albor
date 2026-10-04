#pragma once

#include <cstdint>

struct Component498;

static constexpr int kProp499_Color = 499 * 4 + 0;
static constexpr int kProp499_Opacity = 499 * 4 + 1;
static constexpr int kProp499_Width = 499 * 4 + 2;
static constexpr int kProp499_Height = 499 * 4 + 3;

template <typename Derived>
struct Component499
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component498* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component498* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
