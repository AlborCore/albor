#pragma once

#include <cstdint>

struct Component417;

static constexpr int kProp418_Color = 418 * 4 + 0;
static constexpr int kProp418_Opacity = 418 * 4 + 1;
static constexpr int kProp418_Width = 418 * 4 + 2;
static constexpr int kProp418_Height = 418 * 4 + 3;

template <typename Derived>
struct Component418
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component417* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component417* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
