#include "Component355.h"

#include <cstdint>

struct Component355::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component354* m_child = nullptr;
};

void Component355::set_color(float v) { m_impl->m_color = v; }
void Component355::set_opacity(float v) { m_impl->m_opacity = v; }
void Component355::set_width(float v) { m_impl->m_width = v; }
void Component355::set_height(float v) { m_impl->m_height = v; }
void Component355::set_child(Component354* child) { m_impl->m_child = child; }
void Component355::on_click() {}
void Component355::on_hover() {}
void Component355::on_focus() {}
