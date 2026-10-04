#include "Component439.h"

#include <cstdint>

struct Component439::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component438* m_child = nullptr;
};

Component439::Component439() : m_impl(new Impl) {}
Component439::~Component439() { delete m_impl; }

float Component439::color() const { return m_impl->m_color; }
float Component439::width() const { return m_impl->m_width; }
void Component439::set_color(float v) { m_impl->m_color = v; }
void Component439::set_opacity(float v) { m_impl->m_opacity = v; }
void Component439::set_width(float v) { m_impl->m_width = v; }
void Component439::set_height(float v) { m_impl->m_height = v; }
void Component439::set_child(Component438* child) { m_impl->m_child = child; }
void Component439::on_click() {}
void Component439::on_hover() {}
void Component439::on_focus() {}
