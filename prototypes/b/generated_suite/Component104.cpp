#include "Component104.h"

#include <cstdint>

struct Component104::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component103* m_child = nullptr;
};

Component104::Component104() :
    m_impl(new Impl) {}
Component104::~Component104() { delete m_impl; }

float Component104::color() const { return m_impl->m_color; }
float Component104::width() const { return m_impl->m_width; }
void Component104::set_color(float v) { m_impl->m_color = v; }
void Component104::set_opacity(float v) { m_impl->m_opacity = v; }
void Component104::set_width(float v) { m_impl->m_width = v; }
void Component104::set_height(float v) { m_impl->m_height = v; }
void Component104::set_child(Component103* child) { m_impl->m_child = child; }
void Component104::on_click() {}
void Component104::on_hover() {}
void Component104::on_focus() {}
