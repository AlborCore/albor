#include "Component321.h"

#include <cstdint>

struct Component321::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component320* m_child = nullptr;
};

void Component321::set_color(float v) { m_impl->m_color = v; }
void Component321::set_opacity(float v) { m_impl->m_opacity = v; }
void Component321::set_width(float v) { m_impl->m_width = v; }
void Component321::set_height(float v) { m_impl->m_height = v; }
void Component321::set_child(Component320* child) { m_impl->m_child = child; }
void Component321::on_click() {}
void Component321::on_hover() {}
void Component321::on_focus() {}
