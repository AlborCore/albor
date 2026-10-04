#include "Component208.h"

#include <cstdint>

struct Component208::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component207* m_child = nullptr;
};

Component208::Component208() : m_impl(new Impl) {}
Component208::~Component208() { delete m_impl; }

float Component208::color() const { return m_impl->m_color; }
float Component208::width() const { return m_impl->m_width; }
void Component208::set_color(float v) { m_impl->m_color = v; }
void Component208::set_opacity(float v) { m_impl->m_opacity = v; }
void Component208::set_width(float v) { m_impl->m_width = v; }
void Component208::set_height(float v) { m_impl->m_height = v; }
void Component208::set_child(Component207* child) { m_impl->m_child = child; }
void Component208::on_click() {}
void Component208::on_hover() {}
void Component208::on_focus() {}
