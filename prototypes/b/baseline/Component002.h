#pragma once

#include <cstdint>

template <typename Derived>
struct Component001;

static constexpr int kProp002_Color = 2 * 4 + 0;
static constexpr int kProp002_Opacity = 2 * 4 + 1;
static constexpr int kProp002_Width = 2 * 4 + 2;
static constexpr int kProp002_Height = 2 * 4 + 3;

template <typename Derived>
struct Component002
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component001<Derived>* m_child = nullptr;
    float color() const { return m_color; }
    float width() const { return m_width; }
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component001<Derived>* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
