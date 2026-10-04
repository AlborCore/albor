#include "Component416.h"

#include <cstdint>

struct Component416::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component415* m_child = nullptr;
};

void Component416::set_color(float v) { m_impl->m_color = v; }
void Component416::set_opacity(float v) { m_impl->m_opacity = v; }
void Component416::set_width(float v) { m_impl->m_width = v; }
void Component416::set_height(float v) { m_impl->m_height = v; }
void Component416::set_child(Component415* child) { m_impl->m_child = child; }
void Component416::on_click() {}
void Component416::on_hover() {}
void Component416::on_focus() {}
