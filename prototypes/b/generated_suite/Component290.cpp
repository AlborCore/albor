#include "Component290.h"

#include <cstdint>

struct Component290::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component289* m_child = nullptr;
};

void Component290::set_color(float v) { m_impl->m_color = v; }
void Component290::set_opacity(float v) { m_impl->m_opacity = v; }
void Component290::set_width(float v) { m_impl->m_width = v; }
void Component290::set_height(float v) { m_impl->m_height = v; }
void Component290::set_child(Component289* child) { m_impl->m_child = child; }
void Component290::on_click() {}
void Component290::on_hover() {}
void Component290::on_focus() {}
