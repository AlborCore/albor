#include "Component043.h"

#include <cstdint>

struct Component043::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component042* m_child = nullptr;
};

void Component043::set_color(float v) { m_impl->m_color = v; }
void Component043::set_opacity(float v) { m_impl->m_opacity = v; }
void Component043::set_width(float v) { m_impl->m_width = v; }
void Component043::set_height(float v) { m_impl->m_height = v; }
void Component043::set_child(Component042* child) { m_impl->m_child = child; }
void Component043::on_click() {}
void Component043::on_hover() {}
void Component043::on_focus() {}
