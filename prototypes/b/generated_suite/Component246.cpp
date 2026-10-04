#include "Component246.h"

#include <cstdint>

struct Component246::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component245* m_child = nullptr;
};

Component246::Component246() :
    m_impl(new Impl) {}
Component246::~Component246() { delete m_impl; }

float Component246::color() const { return m_impl->m_color; }
float Component246::width() const { return m_impl->m_width; }
void Component246::set_color(float v) { m_impl->m_color = v; }
void Component246::set_opacity(float v) { m_impl->m_opacity = v; }
void Component246::set_width(float v) { m_impl->m_width = v; }
void Component246::set_height(float v) { m_impl->m_height = v; }
void Component246::set_child(Component245* child) { m_impl->m_child = child; }
void Component246::on_click() {}
void Component246::on_hover() {}
void Component246::on_focus() {}
