#pragma once

#include <cstdint>

struct Component060;

static constexpr int kProp061_Color = 61 * 4 + 0;
static constexpr int kProp061_Opacity = 61 * 4 + 1;
static constexpr int kProp061_Width = 61 * 4 + 2;
static constexpr int kProp061_Height = 61 * 4 + 3;

template <typename Derived>
struct Component061
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component060* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component060* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
