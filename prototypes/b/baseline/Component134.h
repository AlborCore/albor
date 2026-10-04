#pragma once

#include <cstdint>

struct Component133;

static constexpr int kProp134_Color = 134 * 4 + 0;
static constexpr int kProp134_Opacity = 134 * 4 + 1;
static constexpr int kProp134_Width = 134 * 4 + 2;
static constexpr int kProp134_Height = 134 * 4 + 3;

template <typename Derived>
struct Component134
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component133* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component133* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
