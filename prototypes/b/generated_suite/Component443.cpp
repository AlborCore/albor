#include "Component443.h"

#include <cstdint>

struct Component443::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component442* m_child = nullptr;
};

void Component443::set_color(float v) { m_impl->m_color = v; }
void Component443::set_opacity(float v) { m_impl->m_opacity = v; }
void Component443::set_width(float v) { m_impl->m_width = v; }
void Component443::set_height(float v) { m_impl->m_height = v; }
void Component443::set_child(Component442* child) { m_impl->m_child = child; }
void Component443::on_click() {}
void Component443::on_hover() {}
void Component443::on_focus() {}
