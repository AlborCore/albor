#pragma once

#include <cstdint>

struct Component125;

static constexpr int kProp126_Color = 126 * 4 + 0;
static constexpr int kProp126_Opacity = 126 * 4 + 1;
static constexpr int kProp126_Width = 126 * 4 + 2;
static constexpr int kProp126_Height = 126 * 4 + 3;

template <typename Derived>
struct Component126
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component125* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component125* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
