#include "Component270.h"

#include <cstdint>

struct Component270::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component269* m_child = nullptr;
};

void Component270::set_color(float v) { m_impl->m_color = v; }
void Component270::set_opacity(float v) { m_impl->m_opacity = v; }
void Component270::set_width(float v) { m_impl->m_width = v; }
void Component270::set_height(float v) { m_impl->m_height = v; }
void Component270::set_child(Component269* child) { m_impl->m_child = child; }
void Component270::on_click() {}
void Component270::on_hover() {}
void Component270::on_focus() {}
