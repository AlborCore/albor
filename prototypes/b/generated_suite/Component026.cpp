#include "Component026.h"

#include <cstdint>

struct Component026::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component025* m_child = nullptr;
};

void Component026::set_color(float v) { m_impl->m_color = v; }
void Component026::set_opacity(float v) { m_impl->m_opacity = v; }
void Component026::set_width(float v) { m_impl->m_width = v; }
void Component026::set_height(float v) { m_impl->m_height = v; }
void Component026::set_child(Component025* child) { m_impl->m_child = child; }
void Component026::on_click() {}
void Component026::on_hover() {}
void Component026::on_focus() {}
