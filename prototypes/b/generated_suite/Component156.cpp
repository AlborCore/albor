#include "Component156.h"

#include <cstdint>

struct Component156::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component155* m_child = nullptr;
};

void Component156::set_color(float v) { m_impl->m_color = v; }
void Component156::set_opacity(float v) { m_impl->m_opacity = v; }
void Component156::set_width(float v) { m_impl->m_width = v; }
void Component156::set_height(float v) { m_impl->m_height = v; }
void Component156::set_child(Component155* child) { m_impl->m_child = child; }
void Component156::on_click() {}
void Component156::on_hover() {}
void Component156::on_focus() {}
