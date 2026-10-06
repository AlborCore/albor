#pragma once

#include <cstdint>

template <typename Derived>
struct Component071;

static constexpr int kProp072_Color = 72 * 4 + 0;
static constexpr int kProp072_Opacity = 72 * 4 + 1;
static constexpr int kProp072_Width = 72 * 4 + 2;
static constexpr int kProp072_Height = 72 * 4 + 3;

template <typename Derived>
struct Component072
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component071<Derived>* m_child = nullptr;
    float color() const { return m_color; }
    float width() const { return m_width; }
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component071<Derived>* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
