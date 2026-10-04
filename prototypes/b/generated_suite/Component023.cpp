#include "Component023.h"

#include <cstdint>

struct Component023::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component022* m_child = nullptr;
};

void Component023::set_color(float v) { m_impl->m_color = v; }
void Component023::set_opacity(float v) { m_impl->m_opacity = v; }
void Component023::set_width(float v) { m_impl->m_width = v; }
void Component023::set_height(float v) { m_impl->m_height = v; }
void Component023::set_child(Component022* child) { m_impl->m_child = child; }
void Component023::on_click() {}
void Component023::on_hover() {}
void Component023::on_focus() {}
