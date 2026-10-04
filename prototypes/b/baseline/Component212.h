#pragma once

#include <cstdint>

template <typename Derived>
struct Component211;

static constexpr int kProp212_Color = 212 * 4 + 0;
static constexpr int kProp212_Opacity = 212 * 4 + 1;
static constexpr int kProp212_Width = 212 * 4 + 2;
static constexpr int kProp212_Height = 212 * 4 + 3;

template <typename Derived>
struct Component212
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component211<Derived>* m_child = nullptr;
    float color() const { return m_color; }
    float width() const { return m_width; }
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component211<Derived>* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
