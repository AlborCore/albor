#include "Component288.h"

#include <cstdint>

struct Component288::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component287* m_child = nullptr;
};

Component288::Component288() :
    m_impl(new Impl) {}
Component288::~Component288() { delete m_impl; }

float Component288::color() const { return m_impl->m_color; }
float Component288::width() const { return m_impl->m_width; }
void Component288::set_color(float v) { m_impl->m_color = v; }
void Component288::set_opacity(float v) { m_impl->m_opacity = v; }
void Component288::set_width(float v) { m_impl->m_width = v; }
void Component288::set_height(float v) { m_impl->m_height = v; }
void Component288::set_child(Component287* child) { m_impl->m_child = child; }
void Component288::on_click() {}
void Component288::on_hover() {}
void Component288::on_focus() {}
