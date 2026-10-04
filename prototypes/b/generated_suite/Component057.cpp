#include "Component057.h"

#include <cstdint>

struct Component057::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component056* m_child = nullptr;
};

void Component057::set_color(float v) { m_impl->m_color = v; }
void Component057::set_opacity(float v) { m_impl->m_opacity = v; }
void Component057::set_width(float v) { m_impl->m_width = v; }
void Component057::set_height(float v) { m_impl->m_height = v; }
void Component057::set_child(Component056* child) { m_impl->m_child = child; }
void Component057::on_click() {}
void Component057::on_hover() {}
void Component057::on_focus() {}
