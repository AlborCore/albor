#pragma once

#include <cstdint>

struct Component182;

static constexpr int kProp183_Color = 183 * 4 + 0;
static constexpr int kProp183_Opacity = 183 * 4 + 1;
static constexpr int kProp183_Width = 183 * 4 + 2;
static constexpr int kProp183_Height = 183 * 4 + 3;

template <typename Derived>
struct Component183
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component182* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component182* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
