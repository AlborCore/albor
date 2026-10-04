#include "Component423.h"

#include <cstdint>

struct Component423::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component422* m_child = nullptr;
};

void Component423::set_color(float v) { m_impl->m_color = v; }
void Component423::set_opacity(float v) { m_impl->m_opacity = v; }
void Component423::set_width(float v) { m_impl->m_width = v; }
void Component423::set_height(float v) { m_impl->m_height = v; }
void Component423::set_child(Component422* child) { m_impl->m_child = child; }
void Component423::on_click() {}
void Component423::on_hover() {}
void Component423::on_focus() {}
