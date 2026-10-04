#include "Component330.h"

#include <cstdint>

struct Component330::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component329* m_child = nullptr;
};

void Component330::set_color(float v) { m_impl->m_color = v; }
void Component330::set_opacity(float v) { m_impl->m_opacity = v; }
void Component330::set_width(float v) { m_impl->m_width = v; }
void Component330::set_height(float v) { m_impl->m_height = v; }
void Component330::set_child(Component329* child) { m_impl->m_child = child; }
void Component330::on_click() {}
void Component330::on_hover() {}
void Component330::on_focus() {}
