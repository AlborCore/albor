#include "Component034.h"

#include <cstdint>

struct Component034::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component033* m_child = nullptr;
};

void Component034::set_color(float v) { m_impl->m_color = v; }
void Component034::set_opacity(float v) { m_impl->m_opacity = v; }
void Component034::set_width(float v) { m_impl->m_width = v; }
void Component034::set_height(float v) { m_impl->m_height = v; }
void Component034::set_child(Component033* child) { m_impl->m_child = child; }
void Component034::on_click() {}
void Component034::on_hover() {}
void Component034::on_focus() {}
