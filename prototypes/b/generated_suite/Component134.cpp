#include "Component134.h"

#include <cstdint>

struct Component134::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component133* m_child = nullptr;
};

void Component134::set_color(float v) { m_impl->m_color = v; }
void Component134::set_opacity(float v) { m_impl->m_opacity = v; }
void Component134::set_width(float v) { m_impl->m_width = v; }
void Component134::set_height(float v) { m_impl->m_height = v; }
void Component134::set_child(Component133* child) { m_impl->m_child = child; }
void Component134::on_click() {}
void Component134::on_hover() {}
void Component134::on_focus() {}
