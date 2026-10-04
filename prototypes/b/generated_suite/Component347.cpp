#include "Component347.h"

#include <cstdint>

struct Component347::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component346* m_child = nullptr;
};

void Component347::set_color(float v) { m_impl->m_color = v; }
void Component347::set_opacity(float v) { m_impl->m_opacity = v; }
void Component347::set_width(float v) { m_impl->m_width = v; }
void Component347::set_height(float v) { m_impl->m_height = v; }
void Component347::set_child(Component346* child) { m_impl->m_child = child; }
void Component347::on_click() {}
void Component347::on_hover() {}
void Component347::on_focus() {}
