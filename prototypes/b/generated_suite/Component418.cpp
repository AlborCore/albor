#include "Component418.h"

#include <cstdint>

struct Component418::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component417* m_child = nullptr;
};

void Component418::set_color(float v) { m_impl->m_color = v; }
void Component418::set_opacity(float v) { m_impl->m_opacity = v; }
void Component418::set_width(float v) { m_impl->m_width = v; }
void Component418::set_height(float v) { m_impl->m_height = v; }
void Component418::set_child(Component417* child) { m_impl->m_child = child; }
void Component418::on_click() {}
void Component418::on_hover() {}
void Component418::on_focus() {}
