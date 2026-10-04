#include "Component059.h"

#include <cstdint>

struct Component059::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component058* m_child = nullptr;
};

void Component059::set_color(float v) { m_impl->m_color = v; }
void Component059::set_opacity(float v) { m_impl->m_opacity = v; }
void Component059::set_width(float v) { m_impl->m_width = v; }
void Component059::set_height(float v) { m_impl->m_height = v; }
void Component059::set_child(Component058* child) { m_impl->m_child = child; }
void Component059::on_click() {}
void Component059::on_hover() {}
void Component059::on_focus() {}
