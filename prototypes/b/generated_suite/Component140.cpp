#include "Component140.h"

#include <cstdint>

struct Component140::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component139* m_child = nullptr;
};

void Component140::set_color(float v) { m_impl->m_color = v; }
void Component140::set_opacity(float v) { m_impl->m_opacity = v; }
void Component140::set_width(float v) { m_impl->m_width = v; }
void Component140::set_height(float v) { m_impl->m_height = v; }
void Component140::set_child(Component139* child) { m_impl->m_child = child; }
void Component140::on_click() {}
void Component140::on_hover() {}
void Component140::on_focus() {}
