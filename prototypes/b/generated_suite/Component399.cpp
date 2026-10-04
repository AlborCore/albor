#include "Component399.h"

#include <cstdint>

struct Component399::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component398* m_child = nullptr;
};

void Component399::set_color(float v) { m_impl->m_color = v; }
void Component399::set_opacity(float v) { m_impl->m_opacity = v; }
void Component399::set_width(float v) { m_impl->m_width = v; }
void Component399::set_height(float v) { m_impl->m_height = v; }
void Component399::set_child(Component398* child) { m_impl->m_child = child; }
void Component399::on_click() {}
void Component399::on_hover() {}
void Component399::on_focus() {}
