#include "Component456.h"

#include <cstdint>

struct Component456::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component455* m_child = nullptr;
};

void Component456::set_color(float v) { m_impl->m_color = v; }
void Component456::set_opacity(float v) { m_impl->m_opacity = v; }
void Component456::set_width(float v) { m_impl->m_width = v; }
void Component456::set_height(float v) { m_impl->m_height = v; }
void Component456::set_child(Component455* child) { m_impl->m_child = child; }
void Component456::on_click() {}
void Component456::on_hover() {}
void Component456::on_focus() {}
