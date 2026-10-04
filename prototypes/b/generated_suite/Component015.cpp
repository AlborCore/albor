#include "Component015.h"

#include <cstdint>

struct Component015::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component014* m_child = nullptr;
};

void Component015::set_color(float v) { m_impl->m_color = v; }
void Component015::set_opacity(float v) { m_impl->m_opacity = v; }
void Component015::set_width(float v) { m_impl->m_width = v; }
void Component015::set_height(float v) { m_impl->m_height = v; }
void Component015::set_child(Component014* child) { m_impl->m_child = child; }
void Component015::on_click() {}
void Component015::on_hover() {}
void Component015::on_focus() {}
