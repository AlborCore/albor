#include "Component346.h"

#include <cstdint>

struct Component346::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component345* m_child = nullptr;
};

void Component346::set_color(float v) { m_impl->m_color = v; }
void Component346::set_opacity(float v) { m_impl->m_opacity = v; }
void Component346::set_width(float v) { m_impl->m_width = v; }
void Component346::set_height(float v) { m_impl->m_height = v; }
void Component346::set_child(Component345* child) { m_impl->m_child = child; }
void Component346::on_click() {}
void Component346::on_hover() {}
void Component346::on_focus() {}
