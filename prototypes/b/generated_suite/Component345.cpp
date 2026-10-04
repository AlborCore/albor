#include "Component345.h"

#include <cstdint>

struct Component345::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component344* m_child = nullptr;
};

void Component345::set_color(float v) { m_impl->m_color = v; }
void Component345::set_opacity(float v) { m_impl->m_opacity = v; }
void Component345::set_width(float v) { m_impl->m_width = v; }
void Component345::set_height(float v) { m_impl->m_height = v; }
void Component345::set_child(Component344* child) { m_impl->m_child = child; }
void Component345::on_click() {}
void Component345::on_hover() {}
void Component345::on_focus() {}
