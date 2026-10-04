#pragma once

#include <cstdint>

template <typename Derived>
struct Component407;

static constexpr int kProp408_Color = 408 * 4 + 0;
static constexpr int kProp408_Opacity = 408 * 4 + 1;
static constexpr int kProp408_Width = 408 * 4 + 2;
static constexpr int kProp408_Height = 408 * 4 + 3;

template <typename Derived>
struct Component408
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component407<Derived>* m_child = nullptr;
    float color() const { return m_color; }
    float width() const { return m_width; }
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component407<Derived>* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
