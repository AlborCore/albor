#include "Component030.h"

#include <cstdint>

struct Component030::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component029* m_child = nullptr;
};

void Component030::set_color(float v) { m_impl->m_color = v; }
void Component030::set_opacity(float v) { m_impl->m_opacity = v; }
void Component030::set_width(float v) { m_impl->m_width = v; }
void Component030::set_height(float v) { m_impl->m_height = v; }
void Component030::set_child(Component029* child) { m_impl->m_child = child; }
void Component030::on_click() {}
void Component030::on_hover() {}
void Component030::on_focus() {}
