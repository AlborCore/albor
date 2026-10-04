#include "Component200.h"

#include <cstdint>

struct Component200::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component199* m_child = nullptr;
};

void Component200::set_color(float v) { m_impl->m_color = v; }
void Component200::set_opacity(float v) { m_impl->m_opacity = v; }
void Component200::set_width(float v) { m_impl->m_width = v; }
void Component200::set_height(float v) { m_impl->m_height = v; }
void Component200::set_child(Component199* child) { m_impl->m_child = child; }
void Component200::on_click() {}
void Component200::on_hover() {}
void Component200::on_focus() {}
