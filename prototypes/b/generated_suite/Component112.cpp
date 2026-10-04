#include "Component112.h"

#include <cstdint>

struct Component112::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component111* m_child = nullptr;
};

void Component112::set_color(float v) { m_impl->m_color = v; }
void Component112::set_opacity(float v) { m_impl->m_opacity = v; }
void Component112::set_width(float v) { m_impl->m_width = v; }
void Component112::set_height(float v) { m_impl->m_height = v; }
void Component112::set_child(Component111* child) { m_impl->m_child = child; }
void Component112::on_click() {}
void Component112::on_hover() {}
void Component112::on_focus() {}
