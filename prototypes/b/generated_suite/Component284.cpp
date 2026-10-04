#include "Component284.h"

#include <cstdint>

struct Component284::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component283* m_child = nullptr;
};

void Component284::set_color(float v) { m_impl->m_color = v; }
void Component284::set_opacity(float v) { m_impl->m_opacity = v; }
void Component284::set_width(float v) { m_impl->m_width = v; }
void Component284::set_height(float v) { m_impl->m_height = v; }
void Component284::set_child(Component283* child) { m_impl->m_child = child; }
void Component284::on_click() {}
void Component284::on_hover() {}
void Component284::on_focus() {}
