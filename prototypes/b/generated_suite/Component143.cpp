#include "Component143.h"

#include <cstdint>

struct Component143::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component142* m_child = nullptr;
};

void Component143::set_color(float v) { m_impl->m_color = v; }
void Component143::set_opacity(float v) { m_impl->m_opacity = v; }
void Component143::set_width(float v) { m_impl->m_width = v; }
void Component143::set_height(float v) { m_impl->m_height = v; }
void Component143::set_child(Component142* child) { m_impl->m_child = child; }
void Component143::on_click() {}
void Component143::on_hover() {}
void Component143::on_focus() {}
