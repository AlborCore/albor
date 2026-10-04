#include "Component002.h"

#include <cstdint>

struct Component002::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component001* m_child = nullptr;
};

void Component002::set_color(float v) { m_impl->m_color = v; }
void Component002::set_opacity(float v) { m_impl->m_opacity = v; }
void Component002::set_width(float v) { m_impl->m_width = v; }
void Component002::set_height(float v) { m_impl->m_height = v; }
void Component002::set_child(Component001* child) { m_impl->m_child = child; }
void Component002::on_click() {}
void Component002::on_hover() {}
void Component002::on_focus() {}
