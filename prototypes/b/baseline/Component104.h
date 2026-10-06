#pragma once

#include <cstdint>

template <typename Derived>
struct Component103;

static constexpr int kProp104_Color = 104 * 4 + 0;
static constexpr int kProp104_Opacity = 104 * 4 + 1;
static constexpr int kProp104_Width = 104 * 4 + 2;
static constexpr int kProp104_Height = 104 * 4 + 3;

template <typename Derived>
struct Component104
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component103<Derived>* m_child = nullptr;
    float color() const { return m_color; }
    float width() const { return m_width; }
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component103<Derived>* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
