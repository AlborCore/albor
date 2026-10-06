#include "Component430.h"

#include <cstdint>

struct Component430::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component429* m_child = nullptr;
};

Component430::Component430() :
    m_impl(new Impl) {}
Component430::~Component430() { delete m_impl; }

float Component430::color() const { return m_impl->m_color; }
float Component430::width() const { return m_impl->m_width; }
void Component430::set_color(float v) { m_impl->m_color = v; }
void Component430::set_opacity(float v) { m_impl->m_opacity = v; }
void Component430::set_width(float v) { m_impl->m_width = v; }
void Component430::set_height(float v) { m_impl->m_height = v; }
void Component430::set_child(Component429* child) { m_impl->m_child = child; }
void Component430::on_click() {}
void Component430::on_hover() {}
void Component430::on_focus() {}
