#include "Component080.h"

#include <cstdint>

struct Component080::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component079* m_child = nullptr;
};

void Component080::set_color(float v) { m_impl->m_color = v; }
void Component080::set_opacity(float v) { m_impl->m_opacity = v; }
void Component080::set_width(float v) { m_impl->m_width = v; }
void Component080::set_height(float v) { m_impl->m_height = v; }
void Component080::set_child(Component079* child) { m_impl->m_child = child; }
void Component080::on_click() {}
void Component080::on_hover() {}
void Component080::on_focus() {}
