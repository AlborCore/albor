#include "Component215.h"

#include <cstdint>

struct Component215::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component214* m_child = nullptr;
};

void Component215::set_color(float v) { m_impl->m_color = v; }
void Component215::set_opacity(float v) { m_impl->m_opacity = v; }
void Component215::set_width(float v) { m_impl->m_width = v; }
void Component215::set_height(float v) { m_impl->m_height = v; }
void Component215::set_child(Component214* child) { m_impl->m_child = child; }
void Component215::on_click() {}
void Component215::on_hover() {}
void Component215::on_focus() {}
