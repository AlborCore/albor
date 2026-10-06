#include "Component308.h"

#include <cstdint>

struct Component308::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component307* m_child = nullptr;
};

Component308::Component308() :
    m_impl(new Impl) {}
Component308::~Component308() { delete m_impl; }

float Component308::color() const { return m_impl->m_color; }
float Component308::width() const { return m_impl->m_width; }
void Component308::set_color(float v) { m_impl->m_color = v; }
void Component308::set_opacity(float v) { m_impl->m_opacity = v; }
void Component308::set_width(float v) { m_impl->m_width = v; }
void Component308::set_height(float v) { m_impl->m_height = v; }
void Component308::set_child(Component307* child) { m_impl->m_child = child; }
void Component308::on_click() {}
void Component308::on_hover() {}
void Component308::on_focus() {}
