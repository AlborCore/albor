#pragma once

#include <cstdint>

struct Component185;

static constexpr int kProp186_Color = 186 * 4 + 0;
static constexpr int kProp186_Opacity = 186 * 4 + 1;
static constexpr int kProp186_Width = 186 * 4 + 2;
static constexpr int kProp186_Height = 186 * 4 + 3;

template <typename Derived>
struct Component186
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component185* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component185* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
