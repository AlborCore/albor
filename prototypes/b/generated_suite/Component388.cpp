#include "Component388.h"

#include <cstdint>

struct Component388::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component387* m_child = nullptr;
};

void Component388::set_color(float v) { m_impl->m_color = v; }
void Component388::set_opacity(float v) { m_impl->m_opacity = v; }
void Component388::set_width(float v) { m_impl->m_width = v; }
void Component388::set_height(float v) { m_impl->m_height = v; }
void Component388::set_child(Component387* child) { m_impl->m_child = child; }
void Component388::on_click() {}
void Component388::on_hover() {}
void Component388::on_focus() {}
