#include "Component484.h"

#include <cstdint>

struct Component484::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component483* m_child = nullptr;
};

void Component484::set_color(float v) { m_impl->m_color = v; }
void Component484::set_opacity(float v) { m_impl->m_opacity = v; }
void Component484::set_width(float v) { m_impl->m_width = v; }
void Component484::set_height(float v) { m_impl->m_height = v; }
void Component484::set_child(Component483* child) { m_impl->m_child = child; }
void Component484::on_click() {}
void Component484::on_hover() {}
void Component484::on_focus() {}
