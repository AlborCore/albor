#include "Component101.h"

#include <cstdint>

struct Component101::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component100* m_child = nullptr;
};

Component101::Component101() :
    m_impl(new Impl) {}
Component101::~Component101() { delete m_impl; }

float Component101::color() const { return m_impl->m_color; }
float Component101::width() const { return m_impl->m_width; }
void Component101::set_color(float v) { m_impl->m_color = v; }
void Component101::set_opacity(float v) { m_impl->m_opacity = v; }
void Component101::set_width(float v) { m_impl->m_width = v; }
void Component101::set_height(float v) { m_impl->m_height = v; }
void Component101::set_child(Component100* child) { m_impl->m_child = child; }
void Component101::on_click() {}
void Component101::on_hover() {}
void Component101::on_focus() {}
