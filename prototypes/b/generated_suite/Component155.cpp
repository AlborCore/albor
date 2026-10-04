#include "Component155.h"

#include <cstdint>

struct Component155::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component154* m_child = nullptr;
};

void Component155::set_color(float v) { m_impl->m_color = v; }
void Component155::set_opacity(float v) { m_impl->m_opacity = v; }
void Component155::set_width(float v) { m_impl->m_width = v; }
void Component155::set_height(float v) { m_impl->m_height = v; }
void Component155::set_child(Component154* child) { m_impl->m_child = child; }
void Component155::on_click() {}
void Component155::on_hover() {}
void Component155::on_focus() {}
