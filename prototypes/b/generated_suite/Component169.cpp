#include "Component169.h"

#include <cstdint>

struct Component169::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component168* m_child = nullptr;
};

void Component169::set_color(float v) { m_impl->m_color = v; }
void Component169::set_opacity(float v) { m_impl->m_opacity = v; }
void Component169::set_width(float v) { m_impl->m_width = v; }
void Component169::set_height(float v) { m_impl->m_height = v; }
void Component169::set_child(Component168* child) { m_impl->m_child = child; }
void Component169::on_click() {}
void Component169::on_hover() {}
void Component169::on_focus() {}
