#pragma once

#include <cstdint>

struct Component411;

static constexpr int kProp412_Color = 412 * 4 + 0;
static constexpr int kProp412_Opacity = 412 * 4 + 1;
static constexpr int kProp412_Width = 412 * 4 + 2;
static constexpr int kProp412_Height = 412 * 4 + 3;

template <typename Derived>
struct Component412
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component411* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component411* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
