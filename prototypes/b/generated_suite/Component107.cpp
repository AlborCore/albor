#include "Component107.h"

#include <cstdint>

struct Component107::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component106* m_child = nullptr;
};

void Component107::set_color(float v) { m_impl->m_color = v; }
void Component107::set_opacity(float v) { m_impl->m_opacity = v; }
void Component107::set_width(float v) { m_impl->m_width = v; }
void Component107::set_height(float v) { m_impl->m_height = v; }
void Component107::set_child(Component106* child) { m_impl->m_child = child; }
void Component107::on_click() {}
void Component107::on_hover() {}
void Component107::on_focus() {}
