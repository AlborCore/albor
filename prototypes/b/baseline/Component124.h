#pragma once

#include <cstdint>

struct Component123;

static constexpr int kProp124_Color = 124 * 4 + 0;
static constexpr int kProp124_Opacity = 124 * 4 + 1;
static constexpr int kProp124_Width = 124 * 4 + 2;
static constexpr int kProp124_Height = 124 * 4 + 3;

template <typename Derived>
struct Component124
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component123* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component123* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
