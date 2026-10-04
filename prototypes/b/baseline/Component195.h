#pragma once

#include <cstdint>

template <typename Derived>
struct Component194;

static constexpr int kProp195_Color = 195 * 4 + 0;
static constexpr int kProp195_Opacity = 195 * 4 + 1;
static constexpr int kProp195_Width = 195 * 4 + 2;
static constexpr int kProp195_Height = 195 * 4 + 3;

template <typename Derived>
struct Component195
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component194<Derived>* m_child = nullptr;
    float color() const { return m_color; }
    float width() const { return m_width; }
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component194<Derived>* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
