#include "Component017.h"

#include <cstdint>

struct Component017::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component016* m_child = nullptr;
};

void Component017::set_color(float v) { m_impl->m_color = v; }
void Component017::set_opacity(float v) { m_impl->m_opacity = v; }
void Component017::set_width(float v) { m_impl->m_width = v; }
void Component017::set_height(float v) { m_impl->m_height = v; }
void Component017::set_child(Component016* child) { m_impl->m_child = child; }
void Component017::on_click() {}
void Component017::on_hover() {}
void Component017::on_focus() {}
