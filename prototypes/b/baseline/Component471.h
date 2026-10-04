#pragma once

#include <cstdint>

struct Component470;

static constexpr int kProp471_Color = 471 * 4 + 0;
static constexpr int kProp471_Opacity = 471 * 4 + 1;
static constexpr int kProp471_Width = 471 * 4 + 2;
static constexpr int kProp471_Height = 471 * 4 + 3;

template <typename Derived>
struct Component471
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component470* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component470* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
