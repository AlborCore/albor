#include "Component130.h"

#include <cstdint>

struct Component130::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component129* m_child = nullptr;
};

void Component130::set_color(float v) { m_impl->m_color = v; }
void Component130::set_opacity(float v) { m_impl->m_opacity = v; }
void Component130::set_width(float v) { m_impl->m_width = v; }
void Component130::set_height(float v) { m_impl->m_height = v; }
void Component130::set_child(Component129* child) { m_impl->m_child = child; }
void Component130::on_click() {}
void Component130::on_hover() {}
void Component130::on_focus() {}
