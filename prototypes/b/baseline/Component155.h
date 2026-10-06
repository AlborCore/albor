#pragma once

#include <cstdint>

template <typename Derived>
struct Component154;

static constexpr int kProp155_Color = 155 * 4 + 0;
static constexpr int kProp155_Opacity = 155 * 4 + 1;
static constexpr int kProp155_Width = 155 * 4 + 2;
static constexpr int kProp155_Height = 155 * 4 + 3;

template <typename Derived>
struct Component155
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component154<Derived>* m_child = nullptr;
    float color() const { return m_color; }
    float width() const { return m_width; }
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component154<Derived>* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
