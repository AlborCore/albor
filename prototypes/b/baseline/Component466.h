#pragma once

#include <cstdint>

struct Component465;

static constexpr int kProp466_Color = 466 * 4 + 0;
static constexpr int kProp466_Opacity = 466 * 4 + 1;
static constexpr int kProp466_Width = 466 * 4 + 2;
static constexpr int kProp466_Height = 466 * 4 + 3;

template <typename Derived>
struct Component466
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component465* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component465* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
