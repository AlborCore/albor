#include "Component245.h"

#include <cstdint>

struct Component245::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component244* m_child = nullptr;
};

void Component245::set_color(float v) { m_impl->m_color = v; }
void Component245::set_opacity(float v) { m_impl->m_opacity = v; }
void Component245::set_width(float v) { m_impl->m_width = v; }
void Component245::set_height(float v) { m_impl->m_height = v; }
void Component245::set_child(Component244* child) { m_impl->m_child = child; }
void Component245::on_click() {}
void Component245::on_hover() {}
void Component245::on_focus() {}
