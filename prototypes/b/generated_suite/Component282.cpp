#include "Component282.h"

#include <cstdint>

struct Component282::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component281* m_child = nullptr;
};

void Component282::set_color(float v) { m_impl->m_color = v; }
void Component282::set_opacity(float v) { m_impl->m_opacity = v; }
void Component282::set_width(float v) { m_impl->m_width = v; }
void Component282::set_height(float v) { m_impl->m_height = v; }
void Component282::set_child(Component281* child) { m_impl->m_child = child; }
void Component282::on_click() {}
void Component282::on_hover() {}
void Component282::on_focus() {}
