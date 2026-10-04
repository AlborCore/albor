#include "Component453.h"

#include <cstdint>

struct Component453::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component452* m_child = nullptr;
};

Component453::Component453() : m_impl(new Impl) {}
Component453::~Component453() { delete m_impl; }

float Component453::color() const { return m_impl->m_color; }
float Component453::width() const { return m_impl->m_width; }
void Component453::set_color(float v) { m_impl->m_color = v; }
void Component453::set_opacity(float v) { m_impl->m_opacity = v; }
void Component453::set_width(float v) { m_impl->m_width = v; }
void Component453::set_height(float v) { m_impl->m_height = v; }
void Component453::set_child(Component452* child) { m_impl->m_child = child; }
void Component453::on_click() {}
void Component453::on_hover() {}
void Component453::on_focus() {}
