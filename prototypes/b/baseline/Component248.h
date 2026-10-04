#pragma once

#include <cstdint>

struct Component247;

static constexpr int kProp248_Color = 248 * 4 + 0;
static constexpr int kProp248_Opacity = 248 * 4 + 1;
static constexpr int kProp248_Width = 248 * 4 + 2;
static constexpr int kProp248_Height = 248 * 4 + 3;

template <typename Derived>
struct Component248
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component247* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component247* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
