#include "Component458.h"

#include <cstdint>

struct Component458::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component457* m_child = nullptr;
};

Component458::Component458() : m_impl(new Impl) {}
Component458::~Component458() { delete m_impl; }

float Component458::color() const { return m_impl->m_color; }
float Component458::width() const { return m_impl->m_width; }
void Component458::set_color(float v) { m_impl->m_color = v; }
void Component458::set_opacity(float v) { m_impl->m_opacity = v; }
void Component458::set_width(float v) { m_impl->m_width = v; }
void Component458::set_height(float v) { m_impl->m_height = v; }
void Component458::set_child(Component457* child) { m_impl->m_child = child; }
void Component458::on_click() {}
void Component458::on_hover() {}
void Component458::on_focus() {}
