#include "Component116.h"

#include <cstdint>

struct Component116::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component115* m_child = nullptr;
};

void Component116::set_color(float v) { m_impl->m_color = v; }
void Component116::set_opacity(float v) { m_impl->m_opacity = v; }
void Component116::set_width(float v) { m_impl->m_width = v; }
void Component116::set_height(float v) { m_impl->m_height = v; }
void Component116::set_child(Component115* child) { m_impl->m_child = child; }
void Component116::on_click() {}
void Component116::on_hover() {}
void Component116::on_focus() {}
