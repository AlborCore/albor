#pragma once

#include <cstdint>

template <typename Derived>
struct Component042;

static constexpr int kProp043_Color = 43 * 4 + 0;
static constexpr int kProp043_Opacity = 43 * 4 + 1;
static constexpr int kProp043_Width = 43 * 4 + 2;
static constexpr int kProp043_Height = 43 * 4 + 3;

template <typename Derived>
struct Component043
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component042<Derived>* m_child = nullptr;
    float color() const { return m_color; }
    float width() const { return m_width; }
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component042<Derived>* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
