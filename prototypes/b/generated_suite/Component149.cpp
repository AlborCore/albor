#include "Component149.h"

#include <cstdint>

struct Component149::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component148* m_child = nullptr;
};

void Component149::set_color(float v) { m_impl->m_color = v; }
void Component149::set_opacity(float v) { m_impl->m_opacity = v; }
void Component149::set_width(float v) { m_impl->m_width = v; }
void Component149::set_height(float v) { m_impl->m_height = v; }
void Component149::set_child(Component148* child) { m_impl->m_child = child; }
void Component149::on_click() {}
void Component149::on_hover() {}
void Component149::on_focus() {}
