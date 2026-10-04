#include "Component491.h"

#include <cstdint>

struct Component491::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component490* m_child = nullptr;
};

void Component491::set_color(float v) { m_impl->m_color = v; }
void Component491::set_opacity(float v) { m_impl->m_opacity = v; }
void Component491::set_width(float v) { m_impl->m_width = v; }
void Component491::set_height(float v) { m_impl->m_height = v; }
void Component491::set_child(Component490* child) { m_impl->m_child = child; }
void Component491::on_click() {}
void Component491::on_hover() {}
void Component491::on_focus() {}
