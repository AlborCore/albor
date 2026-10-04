#pragma once

#include <cstdint>

struct Component024;

static constexpr int kProp025_Color = 25 * 4 + 0;
static constexpr int kProp025_Opacity = 25 * 4 + 1;
static constexpr int kProp025_Width = 25 * 4 + 2;
static constexpr int kProp025_Height = 25 * 4 + 3;

template <typename Derived>
struct Component025
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component024* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component024* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
