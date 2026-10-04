#include "Component150.h"

#include <cstdint>

struct Component150::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component149* m_child = nullptr;
};

void Component150::set_color(float v) { m_impl->m_color = v; }
void Component150::set_opacity(float v) { m_impl->m_opacity = v; }
void Component150::set_width(float v) { m_impl->m_width = v; }
void Component150::set_height(float v) { m_impl->m_height = v; }
void Component150::set_child(Component149* child) { m_impl->m_child = child; }
void Component150::on_click() {}
void Component150::on_hover() {}
void Component150::on_focus() {}
