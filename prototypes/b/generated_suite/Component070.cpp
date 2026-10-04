#include "Component070.h"

#include <cstdint>

struct Component070::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component069* m_child = nullptr;
};

void Component070::set_color(float v) { m_impl->m_color = v; }
void Component070::set_opacity(float v) { m_impl->m_opacity = v; }
void Component070::set_width(float v) { m_impl->m_width = v; }
void Component070::set_height(float v) { m_impl->m_height = v; }
void Component070::set_child(Component069* child) { m_impl->m_child = child; }
void Component070::on_click() {}
void Component070::on_hover() {}
void Component070::on_focus() {}
