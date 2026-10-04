#include "Component495.h"

#include <cstdint>

struct Component495::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component494* m_child = nullptr;
};

Component495::Component495() :
    m_impl(new Impl) {}
Component495::~Component495() { delete m_impl; }

float Component495::color() const { return m_impl->m_color; }
float Component495::width() const { return m_impl->m_width; }
void Component495::set_color(float v) { m_impl->m_color = v; }
void Component495::set_opacity(float v) { m_impl->m_opacity = v; }
void Component495::set_width(float v) { m_impl->m_width = v; }
void Component495::set_height(float v) { m_impl->m_height = v; }
void Component495::set_child(Component494* child) { m_impl->m_child = child; }
void Component495::on_click() {}
void Component495::on_hover() {}
void Component495::on_focus() {}
