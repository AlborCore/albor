#pragma once

#include <cstdint>

static constexpr int kProp000_Color = 0 * 4 + 0;
static constexpr int kProp000_Opacity = 0 * 4 + 1;
static constexpr int kProp000_Width = 0 * 4 + 2;
static constexpr int kProp000_Height = 0 * 4 + 3;

template <typename Derived>
struct Component000
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    float color() const { return m_color; }
    float width() const { return m_width; }
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
