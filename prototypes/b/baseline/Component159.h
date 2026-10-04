#pragma once

#include <cstdint>

struct Component158;

static constexpr int kProp159_Color = 159 * 4 + 0;
static constexpr int kProp159_Opacity = 159 * 4 + 1;
static constexpr int kProp159_Width = 159 * 4 + 2;
static constexpr int kProp159_Height = 159 * 4 + 3;

template <typename Derived>
struct Component159
{
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component158* m_child = nullptr;
    void set_color(float v) { m_color = v; }
    void set_opacity(float v) { m_opacity = v; }
    void set_width(float v) { m_width = v; }
    void set_height(float v) { m_height = v; }
    void set_child(Component158* child) { m_child = child; }
    void on_click() {}
    void on_hover() {}
    void on_focus() {}
};
