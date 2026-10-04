#include "Component424.h"

#include <cstdint>

struct Component424::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component423* m_child = nullptr;
};

Component424::Component424() : m_impl(new Impl) {}
Component424::~Component424() { delete m_impl; }

float Component424::color() const { return m_impl->m_color; }
float Component424::width() const { return m_impl->m_width; }
void Component424::set_color(float v) { m_impl->m_color = v; }
void Component424::set_opacity(float v) { m_impl->m_opacity = v; }
void Component424::set_width(float v) { m_impl->m_width = v; }
void Component424::set_height(float v) { m_impl->m_height = v; }
void Component424::set_child(Component423* child) { m_impl->m_child = child; }
void Component424::on_click() {}
void Component424::on_hover() {}
void Component424::on_focus() {}
