#include "Component496.h"

#include <cstdint>

struct Component496::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component495* m_child = nullptr;
};

Component496::Component496() :
    m_impl(new Impl) {}
Component496::~Component496() { delete m_impl; }

float Component496::color() const { return m_impl->m_color; }
float Component496::width() const { return m_impl->m_width; }
void Component496::set_color(float v) { m_impl->m_color = v; }
void Component496::set_opacity(float v) { m_impl->m_opacity = v; }
void Component496::set_width(float v) { m_impl->m_width = v; }
void Component496::set_height(float v) { m_impl->m_height = v; }
void Component496::set_child(Component495* child) { m_impl->m_child = child; }
void Component496::on_click() {}
void Component496::on_hover() {}
void Component496::on_focus() {}
