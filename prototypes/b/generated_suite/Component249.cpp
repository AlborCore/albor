#include "Component249.h"

#include <cstdint>

struct Component249::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component248* m_child = nullptr;
};

void Component249::set_color(float v) { m_impl->m_color = v; }
void Component249::set_opacity(float v) { m_impl->m_opacity = v; }
void Component249::set_width(float v) { m_impl->m_width = v; }
void Component249::set_height(float v) { m_impl->m_height = v; }
void Component249::set_child(Component248* child) { m_impl->m_child = child; }
void Component249::on_click() {}
void Component249::on_hover() {}
void Component249::on_focus() {}
