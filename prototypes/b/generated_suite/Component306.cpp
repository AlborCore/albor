#include "Component306.h"

#include <cstdint>

struct Component306::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component305* m_child = nullptr;
};

void Component306::set_color(float v) { m_impl->m_color = v; }
void Component306::set_opacity(float v) { m_impl->m_opacity = v; }
void Component306::set_width(float v) { m_impl->m_width = v; }
void Component306::set_height(float v) { m_impl->m_height = v; }
void Component306::set_child(Component305* child) { m_impl->m_child = child; }
void Component306::on_click() {}
void Component306::on_hover() {}
void Component306::on_focus() {}
