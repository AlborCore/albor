#include "Component109.h"

#include <cstdint>

struct Component109::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component108* m_child = nullptr;
};

void Component109::set_color(float v) { m_impl->m_color = v; }
void Component109::set_opacity(float v) { m_impl->m_opacity = v; }
void Component109::set_width(float v) { m_impl->m_width = v; }
void Component109::set_height(float v) { m_impl->m_height = v; }
void Component109::set_child(Component108* child) { m_impl->m_child = child; }
void Component109::on_click() {}
void Component109::on_hover() {}
void Component109::on_focus() {}
