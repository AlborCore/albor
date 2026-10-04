#include "Component449.h"

#include <cstdint>

struct Component449::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component448* m_child = nullptr;
};

void Component449::set_color(float v) { m_impl->m_color = v; }
void Component449::set_opacity(float v) { m_impl->m_opacity = v; }
void Component449::set_width(float v) { m_impl->m_width = v; }
void Component449::set_height(float v) { m_impl->m_height = v; }
void Component449::set_child(Component448* child) { m_impl->m_child = child; }
void Component449::on_click() {}
void Component449::on_hover() {}
void Component449::on_focus() {}
