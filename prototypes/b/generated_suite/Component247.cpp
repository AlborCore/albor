#include "Component247.h"

#include <cstdint>

struct Component247::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component246* m_child = nullptr;
};

void Component247::set_color(float v) { m_impl->m_color = v; }
void Component247::set_opacity(float v) { m_impl->m_opacity = v; }
void Component247::set_width(float v) { m_impl->m_width = v; }
void Component247::set_height(float v) { m_impl->m_height = v; }
void Component247::set_child(Component246* child) { m_impl->m_child = child; }
void Component247::on_click() {}
void Component247::on_hover() {}
void Component247::on_focus() {}
