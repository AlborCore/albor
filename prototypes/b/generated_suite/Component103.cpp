#include "Component103.h"

#include <cstdint>

struct Component103::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component102* m_child = nullptr;
};

void Component103::set_color(float v) { m_impl->m_color = v; }
void Component103::set_opacity(float v) { m_impl->m_opacity = v; }
void Component103::set_width(float v) { m_impl->m_width = v; }
void Component103::set_height(float v) { m_impl->m_height = v; }
void Component103::set_child(Component102* child) { m_impl->m_child = child; }
void Component103::on_click() {}
void Component103::on_hover() {}
void Component103::on_focus() {}
