#pragma once

#include <cstdint>

struct Component061;

static constexpr int kProp062_Color = 62 * 4 + 0;
static constexpr int kProp062_Opacity = 62 * 4 + 1;
static constexpr int kProp062_Width = 62 * 4 + 2;
static constexpr int kProp062_Height = 62 * 4 + 3;

template <typename Derived>
struct Component062
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component061* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component061* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
