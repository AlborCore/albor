#include "Component172.h"

#include <cstdint>

struct Component172::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component171* m_child = nullptr;
};

void Component172::set_color(float v) { m_impl->m_color = v; }
void Component172::set_opacity(float v) { m_impl->m_opacity = v; }
void Component172::set_width(float v) { m_impl->m_width = v; }
void Component172::set_height(float v) { m_impl->m_height = v; }
void Component172::set_child(Component171* child) { m_impl->m_child = child; }
void Component172::on_click() {}
void Component172::on_hover() {}
void Component172::on_focus() {}
