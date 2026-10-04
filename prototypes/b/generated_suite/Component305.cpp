#include "Component305.h"

#include <cstdint>

struct Component305::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component304* m_child = nullptr;
};

void Component305::set_color(float v) { m_impl->m_color = v; }
void Component305::set_opacity(float v) { m_impl->m_opacity = v; }
void Component305::set_width(float v) { m_impl->m_width = v; }
void Component305::set_height(float v) { m_impl->m_height = v; }
void Component305::set_child(Component304* child) { m_impl->m_child = child; }
void Component305::on_click() {}
void Component305::on_hover() {}
void Component305::on_focus() {}
