#include "Component469.h"

#include <cstdint>

struct Component469::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component468* m_child = nullptr;
};

void Component469::set_color(float v) { m_impl->m_color = v; }
void Component469::set_opacity(float v) { m_impl->m_opacity = v; }
void Component469::set_width(float v) { m_impl->m_width = v; }
void Component469::set_height(float v) { m_impl->m_height = v; }
void Component469::set_child(Component468* child) { m_impl->m_child = child; }
void Component469::on_click() {}
void Component469::on_hover() {}
void Component469::on_focus() {}
