#include "Component151.h"

#include <cstdint>

struct Component151::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component150* m_child = nullptr;
};

void Component151::set_color(float v) { m_impl->m_color = v; }
void Component151::set_opacity(float v) { m_impl->m_opacity = v; }
void Component151::set_width(float v) { m_impl->m_width = v; }
void Component151::set_height(float v) { m_impl->m_height = v; }
void Component151::set_child(Component150* child) { m_impl->m_child = child; }
void Component151::on_click() {}
void Component151::on_hover() {}
void Component151::on_focus() {}
