#pragma once

#include <cstdint>

template <typename Derived>
struct Component437;

static constexpr int kProp438_Color = 438 * 4 + 0;
static constexpr int kProp438_Opacity = 438 * 4 + 1;
static constexpr int kProp438_Width = 438 * 4 + 2;
static constexpr int kProp438_Height = 438 * 4 + 3;

template <typename Derived>
struct Component438
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component437<Derived>* m_child = nullptr;
    float color() const { return m_color; }
    float width() const { return m_width; }
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component437<Derived>* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
