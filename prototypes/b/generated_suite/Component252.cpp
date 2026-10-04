#include "Component252.h"

#include <cstdint>

struct Component252::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component251* m_child = nullptr;
};

void Component252::set_color(float v) { m_impl->m_color = v; }
void Component252::set_opacity(float v) { m_impl->m_opacity = v; }
void Component252::set_width(float v) { m_impl->m_width = v; }
void Component252::set_height(float v) { m_impl->m_height = v; }
void Component252::set_child(Component251* child) { m_impl->m_child = child; }
void Component252::on_click() {}
void Component252::on_hover() {}
void Component252::on_focus() {}
