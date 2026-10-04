#include "Component464.h"

#include <cstdint>

struct Component464::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component463* m_child = nullptr;
};

void Component464::set_color(float v) { m_impl->m_color = v; }
void Component464::set_opacity(float v) { m_impl->m_opacity = v; }
void Component464::set_width(float v) { m_impl->m_width = v; }
void Component464::set_height(float v) { m_impl->m_height = v; }
void Component464::set_child(Component463* child) { m_impl->m_child = child; }
void Component464::on_click() {}
void Component464::on_hover() {}
void Component464::on_focus() {}
