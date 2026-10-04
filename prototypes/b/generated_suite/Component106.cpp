#include "Component106.h"

#include <cstdint>

struct Component106::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component105* m_child = nullptr;
};

void Component106::set_color(float v) { m_impl->m_color = v; }
void Component106::set_opacity(float v) { m_impl->m_opacity = v; }
void Component106::set_width(float v) { m_impl->m_width = v; }
void Component106::set_height(float v) { m_impl->m_height = v; }
void Component106::set_child(Component105* child) { m_impl->m_child = child; }
void Component106::on_click() {}
void Component106::on_hover() {}
void Component106::on_focus() {}
