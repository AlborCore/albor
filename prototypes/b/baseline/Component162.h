#pragma once

#include <cstdint>

template <typename Derived>
struct Component161;

static constexpr int kProp162_Color = 162 * 4 + 0;
static constexpr int kProp162_Opacity = 162 * 4 + 1;
static constexpr int kProp162_Width = 162 * 4 + 2;
static constexpr int kProp162_Height = 162 * 4 + 3;

template <typename Derived>
struct Component162
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component161<Derived>* m_child = nullptr;
    float color() const { return m_color; }
    float width() const { return m_width; }
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component161<Derived>* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
