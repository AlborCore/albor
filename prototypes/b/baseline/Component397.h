#pragma once

#include <cstdint>

template <typename Derived>
struct Component396;

static constexpr int kProp397_Color = 397 * 4 + 0;
static constexpr int kProp397_Opacity = 397 * 4 + 1;
static constexpr int kProp397_Width = 397 * 4 + 2;
static constexpr int kProp397_Height = 397 * 4 + 3;

template <typename Derived>
struct Component397
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component396<Derived>* m_child = nullptr;
    float color() const { return m_color; }
    float width() const { return m_width; }
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component396<Derived>* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
