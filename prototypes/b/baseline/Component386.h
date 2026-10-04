#pragma once

#include <cstdint>

struct Component385;

static constexpr int kProp386_Color = 386 * 4 + 0;
static constexpr int kProp386_Opacity = 386 * 4 + 1;
static constexpr int kProp386_Width = 386 * 4 + 2;
static constexpr int kProp386_Height = 386 * 4 + 3;

template <typename Derived>
struct Component386
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component385* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component385* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
