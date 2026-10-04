#include "Component415.h"

#include <cstdint>

struct Component415::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component414* m_child = nullptr;
};

void Component415::set_color(float v) { m_impl->m_color = v; }
void Component415::set_opacity(float v) { m_impl->m_opacity = v; }
void Component415::set_width(float v) { m_impl->m_width = v; }
void Component415::set_height(float v) { m_impl->m_height = v; }
void Component415::set_child(Component414* child) { m_impl->m_child = child; }
void Component415::on_click() {}
void Component415::on_hover() {}
void Component415::on_focus() {}
