#include "Component336.h"

#include <cstdint>

struct Component336::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component335* m_child = nullptr;
};

void Component336::set_color(float v) { m_impl->m_color = v; }
void Component336::set_opacity(float v) { m_impl->m_opacity = v; }
void Component336::set_width(float v) { m_impl->m_width = v; }
void Component336::set_height(float v) { m_impl->m_height = v; }
void Component336::set_child(Component335* child) { m_impl->m_child = child; }
void Component336::on_click() {}
void Component336::on_hover() {}
void Component336::on_focus() {}
