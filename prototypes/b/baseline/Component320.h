#pragma once

#include <cstdint>

struct Component319;

static constexpr int kProp320_Color = 320 * 4 + 0;
static constexpr int kProp320_Opacity = 320 * 4 + 1;
static constexpr int kProp320_Width = 320 * 4 + 2;
static constexpr int kProp320_Height = 320 * 4 + 3;

template <typename Derived>
struct Component320
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component319* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component319* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
