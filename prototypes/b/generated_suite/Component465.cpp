#include "Component465.h"

#include <cstdint>

struct Component465::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component464* m_child = nullptr;
};

void Component465::set_color(float v) { m_impl->m_color = v; }
void Component465::set_opacity(float v) { m_impl->m_opacity = v; }
void Component465::set_width(float v) { m_impl->m_width = v; }
void Component465::set_height(float v) { m_impl->m_height = v; }
void Component465::set_child(Component464* child) { m_impl->m_child = child; }
void Component465::on_click() {}
void Component465::on_hover() {}
void Component465::on_focus() {}
