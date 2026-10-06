#pragma once

#include <cstdint>

template <typename Derived>
struct Component300;

static constexpr int kProp301_Color = 301 * 4 + 0;
static constexpr int kProp301_Opacity = 301 * 4 + 1;
static constexpr int kProp301_Width = 301 * 4 + 2;
static constexpr int kProp301_Height = 301 * 4 + 3;

template <typename Derived>
struct Component301
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component300<Derived>* m_child = nullptr;
    float color() const { return m_color; }
    float width() const { return m_width; }
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component300<Derived>* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
