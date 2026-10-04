#include "Component263.h"

#include <cstdint>

struct Component263::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component262* m_child = nullptr;
};

void Component263::set_color(float v) { m_impl->m_color = v; }
void Component263::set_opacity(float v) { m_impl->m_opacity = v; }
void Component263::set_width(float v) { m_impl->m_width = v; }
void Component263::set_height(float v) { m_impl->m_height = v; }
void Component263::set_child(Component262* child) { m_impl->m_child = child; }
void Component263::on_click() {}
void Component263::on_hover() {}
void Component263::on_focus() {}
