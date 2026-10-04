#pragma once

#include <cstdint>

template <typename Derived>
struct Component380;

static constexpr int kProp381_Color = 381 * 4 + 0;
static constexpr int kProp381_Opacity = 381 * 4 + 1;
static constexpr int kProp381_Width = 381 * 4 + 2;
static constexpr int kProp381_Height = 381 * 4 + 3;

template <typename Derived>
struct Component381
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component380<Derived>* m_child = nullptr;
    float color() const { return m_color; }
    float width() const { return m_width; }
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component380<Derived>* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
