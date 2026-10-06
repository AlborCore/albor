#pragma once

#include <cstdint>

template <typename Derived>
struct Component048;

static constexpr int kProp049_Color = 49 * 4 + 0;
static constexpr int kProp049_Opacity = 49 * 4 + 1;
static constexpr int kProp049_Width = 49 * 4 + 2;
static constexpr int kProp049_Height = 49 * 4 + 3;

template <typename Derived>
struct Component049
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component048<Derived>* m_child = nullptr;
    float color() const { return m_color; }
    float width() const { return m_width; }
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component048<Derived>* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
