#include "Component110.h"

#include <cstdint>

struct Component110::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component109* m_child = nullptr;
};

void Component110::set_color(float v) { m_impl->m_color = v; }
void Component110::set_opacity(float v) { m_impl->m_opacity = v; }
void Component110::set_width(float v) { m_impl->m_width = v; }
void Component110::set_height(float v) { m_impl->m_height = v; }
void Component110::set_child(Component109* child) { m_impl->m_child = child; }
void Component110::on_click() {}
void Component110::on_hover() {}
void Component110::on_focus() {}
