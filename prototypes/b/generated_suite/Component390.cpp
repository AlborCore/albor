#include "Component390.h"

#include <cstdint>

struct Component390::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component389* m_child = nullptr;
};

void Component390::set_color(float v) { m_impl->m_color = v; }
void Component390::set_opacity(float v) { m_impl->m_opacity = v; }
void Component390::set_width(float v) { m_impl->m_width = v; }
void Component390::set_height(float v) { m_impl->m_height = v; }
void Component390::set_child(Component389* child) { m_impl->m_child = child; }
void Component390::on_click() {}
void Component390::on_hover() {}
void Component390::on_focus() {}
