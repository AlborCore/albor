#include "Component205.h"

#include <cstdint>

struct Component205::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component204* m_child = nullptr;
};

void Component205::set_color(float v) { m_impl->m_color = v; }
void Component205::set_opacity(float v) { m_impl->m_opacity = v; }
void Component205::set_width(float v) { m_impl->m_width = v; }
void Component205::set_height(float v) { m_impl->m_height = v; }
void Component205::set_child(Component204* child) { m_impl->m_child = child; }
void Component205::on_click() {}
void Component205::on_hover() {}
void Component205::on_focus() {}
