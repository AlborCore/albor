#include "Component377.h"

#include <cstdint>

struct Component377::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component376* m_child = nullptr;
};

void Component377::set_color(float v) { m_impl->m_color = v; }
void Component377::set_opacity(float v) { m_impl->m_opacity = v; }
void Component377::set_width(float v) { m_impl->m_width = v; }
void Component377::set_height(float v) { m_impl->m_height = v; }
void Component377::set_child(Component376* child) { m_impl->m_child = child; }
void Component377::on_click() {}
void Component377::on_hover() {}
void Component377::on_focus() {}
