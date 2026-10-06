#include "Component057.h"

#include <cstdint>

struct Component057::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component056* m_child = nullptr;
};

Component057::Component057() :
    m_impl(new Impl) {}
Component057::~Component057() { delete m_impl; }

float Component057::color() const { return m_impl->m_color; }
float Component057::width() const { return m_impl->m_width; }
void Component057::set_color(float v) { m_impl->m_color = v; }
void Component057::set_opacity(float v) { m_impl->m_opacity = v; }
void Component057::set_width(float v) { m_impl->m_width = v; }
void Component057::set_height(float v) { m_impl->m_height = v; }
void Component057::set_child(Component056* child) { m_impl->m_child = child; }
void Component057::on_click() {}
void Component057::on_hover() {}
void Component057::on_focus() {}
