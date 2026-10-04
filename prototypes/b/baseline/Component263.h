#pragma once

#include <cstdint>

template <typename Derived>
struct Component262;

static constexpr int kProp263_Color = 263 * 4 + 0;
static constexpr int kProp263_Opacity = 263 * 4 + 1;
static constexpr int kProp263_Width = 263 * 4 + 2;
static constexpr int kProp263_Height = 263 * 4 + 3;

template <typename Derived>
struct Component263
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component262<Derived>* m_child = nullptr;
    float color() const { return m_color; }
    float width() const { return m_width; }
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component262<Derived>* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
