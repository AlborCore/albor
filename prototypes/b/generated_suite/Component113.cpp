#include "Component113.h"

#include <cstdint>

struct Component113::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component112* m_child = nullptr;
};

void Component113::set_color(float v) { m_impl->m_color = v; }
void Component113::set_opacity(float v) { m_impl->m_opacity = v; }
void Component113::set_width(float v) { m_impl->m_width = v; }
void Component113::set_height(float v) { m_impl->m_height = v; }
void Component113::set_child(Component112* child) { m_impl->m_child = child; }
void Component113::on_click() {}
void Component113::on_hover() {}
void Component113::on_focus() {}
