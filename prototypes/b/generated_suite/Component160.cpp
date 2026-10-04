#include "Component160.h"

#include <cstdint>

struct Component160::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component159* m_child = nullptr;
};

void Component160::set_color(float v) { m_impl->m_color = v; }
void Component160::set_opacity(float v) { m_impl->m_opacity = v; }
void Component160::set_width(float v) { m_impl->m_width = v; }
void Component160::set_height(float v) { m_impl->m_height = v; }
void Component160::set_child(Component159* child) { m_impl->m_child = child; }
void Component160::on_click() {}
void Component160::on_hover() {}
void Component160::on_focus() {}
