#include "Component056.h"

#include <cstdint>

struct Component056::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component055* m_child = nullptr;
};

void Component056::set_color(float v) { m_impl->m_color = v; }
void Component056::set_opacity(float v) { m_impl->m_opacity = v; }
void Component056::set_width(float v) { m_impl->m_width = v; }
void Component056::set_height(float v) { m_impl->m_height = v; }
void Component056::set_child(Component055* child) { m_impl->m_child = child; }
void Component056::on_click() {}
void Component056::on_hover() {}
void Component056::on_focus() {}
