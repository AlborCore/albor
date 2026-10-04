#pragma once

#include <cstdint>

template <typename Derived>
struct Component046;

static constexpr int kProp047_Color = 47 * 4 + 0;
static constexpr int kProp047_Opacity = 47 * 4 + 1;
static constexpr int kProp047_Width = 47 * 4 + 2;
static constexpr int kProp047_Height = 47 * 4 + 3;

template <typename Derived>
struct Component047
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component046<Derived>* m_child = nullptr;
    float color() const { return m_color; }
    float width() const { return m_width; }
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component046<Derived>* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
