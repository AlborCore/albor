#include "Component488.h"

#include <cstdint>

struct Component488::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component487* m_child = nullptr;
};

void Component488::set_color(float v) { m_impl->m_color = v; }
void Component488::set_opacity(float v) { m_impl->m_opacity = v; }
void Component488::set_width(float v) { m_impl->m_width = v; }
void Component488::set_height(float v) { m_impl->m_height = v; }
void Component488::set_child(Component487* child) { m_impl->m_child = child; }
void Component488::on_click() {}
void Component488::on_hover() {}
void Component488::on_focus() {}
