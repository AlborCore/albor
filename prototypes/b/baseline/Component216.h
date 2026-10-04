#pragma once

#include <cstdint>

struct Component215;

static constexpr int kProp216_Color = 216 * 4 + 0;
static constexpr int kProp216_Opacity = 216 * 4 + 1;
static constexpr int kProp216_Width = 216 * 4 + 2;
static constexpr int kProp216_Height = 216 * 4 + 3;

template <typename Derived>
struct Component216
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component215* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component215* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
