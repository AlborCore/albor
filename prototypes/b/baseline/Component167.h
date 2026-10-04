#pragma once

#include <cstdint>

struct Component166;

static constexpr int kProp167_Color = 167 * 4 + 0;
static constexpr int kProp167_Opacity = 167 * 4 + 1;
static constexpr int kProp167_Width = 167 * 4 + 2;
static constexpr int kProp167_Height = 167 * 4 + 3;

template <typename Derived>
struct Component167
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component166* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component166* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
