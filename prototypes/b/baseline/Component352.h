#pragma once

#include <cstdint>

struct Component351;

static constexpr int kProp352_Color = 352 * 4 + 0;
static constexpr int kProp352_Opacity = 352 * 4 + 1;
static constexpr int kProp352_Width = 352 * 4 + 2;
static constexpr int kProp352_Height = 352 * 4 + 3;

template <typename Derived>
struct Component352
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component351* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component351* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
