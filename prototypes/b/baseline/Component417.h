#pragma once

#include <cstdint>

struct Component416;

static constexpr int kProp417_Color = 417 * 4 + 0;
static constexpr int kProp417_Opacity = 417 * 4 + 1;
static constexpr int kProp417_Width = 417 * 4 + 2;
static constexpr int kProp417_Height = 417 * 4 + 3;

template <typename Derived>
struct Component417
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component416* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component416* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
