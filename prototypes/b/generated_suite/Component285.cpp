#include "Component285.h"

#include <cstdint>

struct Component285::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component284* m_child = nullptr;
};

void Component285::set_color(float v) { m_impl->m_color = v; }
void Component285::set_opacity(float v) { m_impl->m_opacity = v; }
void Component285::set_width(float v) { m_impl->m_width = v; }
void Component285::set_height(float v) { m_impl->m_height = v; }
void Component285::set_child(Component284* child) { m_impl->m_child = child; }
void Component285::on_click() {}
void Component285::on_hover() {}
void Component285::on_focus() {}
