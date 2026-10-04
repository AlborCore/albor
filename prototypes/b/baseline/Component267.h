#pragma once

#include <cstdint>

struct Component266;

static constexpr int kProp267_Color = 267 * 4 + 0;
static constexpr int kProp267_Opacity = 267 * 4 + 1;
static constexpr int kProp267_Width = 267 * 4 + 2;
static constexpr int kProp267_Height = 267 * 4 + 3;

template <typename Derived>
struct Component267
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component266* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component266* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
