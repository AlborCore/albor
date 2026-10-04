#pragma once

#include <cstdint>

struct Component394;

static constexpr int kProp395_Color = 395 * 4 + 0;
static constexpr int kProp395_Opacity = 395 * 4 + 1;
static constexpr int kProp395_Width = 395 * 4 + 2;
static constexpr int kProp395_Height = 395 * 4 + 3;

template <typename Derived>
struct Component395
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component394* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component394* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
