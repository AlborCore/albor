#include "Component480.h"

#include <cstdint>

struct Component480::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component479* m_child = nullptr;
};

void Component480::set_color(float v) { m_impl->m_color = v; }
void Component480::set_opacity(float v) { m_impl->m_opacity = v; }
void Component480::set_width(float v) { m_impl->m_width = v; }
void Component480::set_height(float v) { m_impl->m_height = v; }
void Component480::set_child(Component479* child) { m_impl->m_child = child; }
void Component480::on_click() {}
void Component480::on_hover() {}
void Component480::on_focus() {}
