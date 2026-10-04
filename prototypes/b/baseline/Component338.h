#pragma once

#include <cstdint>

template <typename Derived>
struct Component337;

static constexpr int kProp338_Color = 338 * 4 + 0;
static constexpr int kProp338_Opacity = 338 * 4 + 1;
static constexpr int kProp338_Width = 338 * 4 + 2;
static constexpr int kProp338_Height = 338 * 4 + 3;

template <typename Derived>
struct Component338
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component337<Derived>* m_child = nullptr;
    float color() const { return m_color; }
    float width() const { return m_width; }
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component337<Derived>* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
