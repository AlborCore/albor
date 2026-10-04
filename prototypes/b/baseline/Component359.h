#pragma once

#include <cstdint>

struct Component358;

static constexpr int kProp359_Color = 359 * 4 + 0;
static constexpr int kProp359_Opacity = 359 * 4 + 1;
static constexpr int kProp359_Width = 359 * 4 + 2;
static constexpr int kProp359_Height = 359 * 4 + 3;

template <typename Derived>
struct Component359
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component358* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component358* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
