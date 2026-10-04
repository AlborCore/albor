#pragma once

#include <cstdint>

struct Component259;

static constexpr int kProp260_Color = 260 * 4 + 0;
static constexpr int kProp260_Opacity = 260 * 4 + 1;
static constexpr int kProp260_Width = 260 * 4 + 2;
static constexpr int kProp260_Height = 260 * 4 + 3;

template <typename Derived>
struct Component260
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component259* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component259* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
