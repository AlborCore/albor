#include "Component297.h"

#include <cstdint>

struct Component297::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component296* m_child = nullptr;
};

void Component297::set_color(float v) { m_impl->m_color = v; }
void Component297::set_opacity(float v) { m_impl->m_opacity = v; }
void Component297::set_width(float v) { m_impl->m_width = v; }
void Component297::set_height(float v) { m_impl->m_height = v; }
void Component297::set_child(Component296* child) { m_impl->m_child = child; }
void Component297::on_click() {}
void Component297::on_hover() {}
void Component297::on_focus() {}
