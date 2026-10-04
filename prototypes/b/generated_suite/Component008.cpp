#include "Component008.h"

#include <cstdint>

struct Component008::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component007* m_child = nullptr;
};

void Component008::set_color(float v) { m_impl->m_color = v; }
void Component008::set_opacity(float v) { m_impl->m_opacity = v; }
void Component008::set_width(float v) { m_impl->m_width = v; }
void Component008::set_height(float v) { m_impl->m_height = v; }
void Component008::set_child(Component007* child) { m_impl->m_child = child; }
void Component008::on_click() {}
void Component008::on_hover() {}
void Component008::on_focus() {}
