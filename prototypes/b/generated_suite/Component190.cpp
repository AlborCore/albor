#include "Component190.h"

#include <cstdint>

struct Component190::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component189* m_child = nullptr;
};

void Component190::set_color(float v) { m_impl->m_color = v; }
void Component190::set_opacity(float v) { m_impl->m_opacity = v; }
void Component190::set_width(float v) { m_impl->m_width = v; }
void Component190::set_height(float v) { m_impl->m_height = v; }
void Component190::set_child(Component189* child) { m_impl->m_child = child; }
void Component190::on_click() {}
void Component190::on_hover() {}
void Component190::on_focus() {}
