#include "Component392.h"

#include <cstdint>

struct Component392::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component391* m_child = nullptr;
};

void Component392::set_color(float v) { m_impl->m_color = v; }
void Component392::set_opacity(float v) { m_impl->m_opacity = v; }
void Component392::set_width(float v) { m_impl->m_width = v; }
void Component392::set_height(float v) { m_impl->m_height = v; }
void Component392::set_child(Component391* child) { m_impl->m_child = child; }
void Component392::on_click() {}
void Component392::on_hover() {}
void Component392::on_focus() {}
