#include "Component195.h"

#include <cstdint>

struct Component195::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component194* m_child = nullptr;
};

void Component195::set_color(float v) { m_impl->m_color = v; }
void Component195::set_opacity(float v) { m_impl->m_opacity = v; }
void Component195::set_width(float v) { m_impl->m_width = v; }
void Component195::set_height(float v) { m_impl->m_height = v; }
void Component195::set_child(Component194* child) { m_impl->m_child = child; }
void Component195::on_click() {}
void Component195::on_hover() {}
void Component195::on_focus() {}
