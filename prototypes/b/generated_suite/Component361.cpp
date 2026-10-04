#include "Component361.h"

#include <cstdint>

struct Component361::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component360* m_child = nullptr;
};

void Component361::set_color(float v) { m_impl->m_color = v; }
void Component361::set_opacity(float v) { m_impl->m_opacity = v; }
void Component361::set_width(float v) { m_impl->m_width = v; }
void Component361::set_height(float v) { m_impl->m_height = v; }
void Component361::set_child(Component360* child) { m_impl->m_child = child; }
void Component361::on_click() {}
void Component361::on_hover() {}
void Component361::on_focus() {}
