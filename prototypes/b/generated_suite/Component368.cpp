#include "Component368.h"

#include <cstdint>

struct Component368::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component367* m_child = nullptr;
};

void Component368::set_color(float v) { m_impl->m_color = v; }
void Component368::set_opacity(float v) { m_impl->m_opacity = v; }
void Component368::set_width(float v) { m_impl->m_width = v; }
void Component368::set_height(float v) { m_impl->m_height = v; }
void Component368::set_child(Component367* child) { m_impl->m_child = child; }
void Component368::on_click() {}
void Component368::on_hover() {}
void Component368::on_focus() {}
