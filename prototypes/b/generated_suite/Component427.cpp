#include "Component427.h"

#include <cstdint>

struct Component427::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component426* m_child = nullptr;
};

void Component427::set_color(float v) { m_impl->m_color = v; }
void Component427::set_opacity(float v) { m_impl->m_opacity = v; }
void Component427::set_width(float v) { m_impl->m_width = v; }
void Component427::set_height(float v) { m_impl->m_height = v; }
void Component427::set_child(Component426* child) { m_impl->m_child = child; }
void Component427::on_click() {}
void Component427::on_hover() {}
void Component427::on_focus() {}
