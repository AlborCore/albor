#include "Component324.h"

#include <cstdint>

struct Component324::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component323* m_child = nullptr;
};

void Component324::set_color(float v) { m_impl->m_color = v; }
void Component324::set_opacity(float v) { m_impl->m_opacity = v; }
void Component324::set_width(float v) { m_impl->m_width = v; }
void Component324::set_height(float v) { m_impl->m_height = v; }
void Component324::set_child(Component323* child) { m_impl->m_child = child; }
void Component324::on_click() {}
void Component324::on_hover() {}
void Component324::on_focus() {}
