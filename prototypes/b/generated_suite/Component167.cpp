#include "Component167.h"

#include <cstdint>

struct Component167::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component166* m_child = nullptr;
};

void Component167::set_color(float v) { m_impl->m_color = v; }
void Component167::set_opacity(float v) { m_impl->m_opacity = v; }
void Component167::set_width(float v) { m_impl->m_width = v; }
void Component167::set_height(float v) { m_impl->m_height = v; }
void Component167::set_child(Component166* child) { m_impl->m_child = child; }
void Component167::on_click() {}
void Component167::on_hover() {}
void Component167::on_focus() {}
