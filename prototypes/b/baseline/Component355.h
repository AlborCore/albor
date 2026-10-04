#pragma once

#include <cstdint>

struct Component354;

static constexpr int kProp355_Color = 355 * 4 + 0;
static constexpr int kProp355_Opacity = 355 * 4 + 1;
static constexpr int kProp355_Width = 355 * 4 + 2;
static constexpr int kProp355_Height = 355 * 4 + 3;

template <typename Derived>
struct Component355
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component354* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component354* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
