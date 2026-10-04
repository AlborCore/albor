#include "Component164.h"

#include <cstdint>

struct Component164::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component163* m_child = nullptr;
};

Component164::Component164() : m_impl(new Impl) {}
Component164::~Component164() { delete m_impl; }

float Component164::color() const { return m_impl->m_color; }
float Component164::width() const { return m_impl->m_width; }
void Component164::set_color(float v) { m_impl->m_color = v; }
void Component164::set_opacity(float v) { m_impl->m_opacity = v; }
void Component164::set_width(float v) { m_impl->m_width = v; }
void Component164::set_height(float v) { m_impl->m_height = v; }
void Component164::set_child(Component163* child) { m_impl->m_child = child; }
void Component164::on_click() {}
void Component164::on_hover() {}
void Component164::on_focus() {}
