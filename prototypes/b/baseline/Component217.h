#pragma once

#include <cstdint>

struct Component216;

static constexpr int kProp217_Color = 217 * 4 + 0;
static constexpr int kProp217_Opacity = 217 * 4 + 1;
static constexpr int kProp217_Width = 217 * 4 + 2;
static constexpr int kProp217_Height = 217 * 4 + 3;

template <typename Derived>
struct Component217
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component216* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component216* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
