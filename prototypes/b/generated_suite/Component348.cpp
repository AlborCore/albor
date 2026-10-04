#include "Component348.h"

#include <cstdint>

struct Component348::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component347* m_child = nullptr;
};

void Component348::set_color(float v) { m_impl->m_color = v; }
void Component348::set_opacity(float v) { m_impl->m_opacity = v; }
void Component348::set_width(float v) { m_impl->m_width = v; }
void Component348::set_height(float v) { m_impl->m_height = v; }
void Component348::set_child(Component347* child) { m_impl->m_child = child; }
void Component348::on_click() {}
void Component348::on_hover() {}
void Component348::on_focus() {}
