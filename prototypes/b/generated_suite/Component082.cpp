#include "Component082.h"

#include <cstdint>

struct Component082::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component081* m_child = nullptr;
};

void Component082::set_color(float v) { m_impl->m_color = v; }
void Component082::set_opacity(float v) { m_impl->m_opacity = v; }
void Component082::set_width(float v) { m_impl->m_width = v; }
void Component082::set_height(float v) { m_impl->m_height = v; }
void Component082::set_child(Component081* child) { m_impl->m_child = child; }
void Component082::on_click() {}
void Component082::on_hover() {}
void Component082::on_focus() {}
