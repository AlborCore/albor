#include "Component129.h"

#include <cstdint>

struct Component129::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component128* m_child = nullptr;
};

void Component129::set_color(float v) { m_impl->m_color = v; }
void Component129::set_opacity(float v) { m_impl->m_opacity = v; }
void Component129::set_width(float v) { m_impl->m_width = v; }
void Component129::set_height(float v) { m_impl->m_height = v; }
void Component129::set_child(Component128* child) { m_impl->m_child = child; }
void Component129::on_click() {}
void Component129::on_hover() {}
void Component129::on_focus() {}
