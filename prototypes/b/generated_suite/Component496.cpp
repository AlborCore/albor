#include "Component496.h"

#include <cstdint>

struct Component496::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component495* m_child = nullptr;
};

void Component496::set_color(float v) { m_impl->m_color = v; }
void Component496::set_opacity(float v) { m_impl->m_opacity = v; }
void Component496::set_width(float v) { m_impl->m_width = v; }
void Component496::set_height(float v) { m_impl->m_height = v; }
void Component496::set_child(Component495* child) { m_impl->m_child = child; }
void Component496::on_click() {}
void Component496::on_hover() {}
void Component496::on_focus() {}
