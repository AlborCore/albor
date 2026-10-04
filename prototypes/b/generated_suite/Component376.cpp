#include "Component376.h"

#include <cstdint>

struct Component376::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component375* m_child = nullptr;
};

void Component376::set_color(float v) { m_impl->m_color = v; }
void Component376::set_opacity(float v) { m_impl->m_opacity = v; }
void Component376::set_width(float v) { m_impl->m_width = v; }
void Component376::set_height(float v) { m_impl->m_height = v; }
void Component376::set_child(Component375* child) { m_impl->m_child = child; }
void Component376::on_click() {}
void Component376::on_hover() {}
void Component376::on_focus() {}
