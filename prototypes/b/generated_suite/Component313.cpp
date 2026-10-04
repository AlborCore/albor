#include "Component313.h"

#include <cstdint>

struct Component313::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component312* m_child = nullptr;
};

void Component313::set_color(float v) { m_impl->m_color = v; }
void Component313::set_opacity(float v) { m_impl->m_opacity = v; }
void Component313::set_width(float v) { m_impl->m_width = v; }
void Component313::set_height(float v) { m_impl->m_height = v; }
void Component313::set_child(Component312* child) { m_impl->m_child = child; }
void Component313::on_click() {}
void Component313::on_hover() {}
void Component313::on_focus() {}
