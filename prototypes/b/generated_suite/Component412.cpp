#include "Component412.h"

#include <cstdint>

struct Component412::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component411* m_child = nullptr;
};

void Component412::set_color(float v) { m_impl->m_color = v; }
void Component412::set_opacity(float v) { m_impl->m_opacity = v; }
void Component412::set_width(float v) { m_impl->m_width = v; }
void Component412::set_height(float v) { m_impl->m_height = v; }
void Component412::set_child(Component411* child) { m_impl->m_child = child; }
void Component412::on_click() {}
void Component412::on_hover() {}
void Component412::on_focus() {}
