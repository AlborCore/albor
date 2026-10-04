#include "Component289.h"

#include <cstdint>

struct Component289::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component288* m_child = nullptr;
};

void Component289::set_color(float v) { m_impl->m_color = v; }
void Component289::set_opacity(float v) { m_impl->m_opacity = v; }
void Component289::set_width(float v) { m_impl->m_width = v; }
void Component289::set_height(float v) { m_impl->m_height = v; }
void Component289::set_child(Component288* child) { m_impl->m_child = child; }
void Component289::on_click() {}
void Component289::on_hover() {}
void Component289::on_focus() {}
