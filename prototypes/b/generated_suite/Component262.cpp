#include "Component262.h"

#include <cstdint>

struct Component262::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component261* m_child = nullptr;
};

void Component262::set_color(float v) { m_impl->m_color = v; }
void Component262::set_opacity(float v) { m_impl->m_opacity = v; }
void Component262::set_width(float v) { m_impl->m_width = v; }
void Component262::set_height(float v) { m_impl->m_height = v; }
void Component262::set_child(Component261* child) { m_impl->m_child = child; }
void Component262::on_click() {}
void Component262::on_hover() {}
void Component262::on_focus() {}
