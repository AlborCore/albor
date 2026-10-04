#include "Component163.h"

#include <cstdint>

struct Component163::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component162* m_child = nullptr;
};

Component163::Component163() : m_impl(new Impl) {}
Component163::~Component163() { delete m_impl; }

float Component163::color() const { return m_impl->m_color; }
float Component163::width() const { return m_impl->m_width; }
void Component163::set_color(float v) { m_impl->m_color = v; }
void Component163::set_opacity(float v) { m_impl->m_opacity = v; }
void Component163::set_width(float v) { m_impl->m_width = v; }
void Component163::set_height(float v) { m_impl->m_height = v; }
void Component163::set_child(Component162* child) { m_impl->m_child = child; }
void Component163::on_click() {}
void Component163::on_hover() {}
void Component163::on_focus() {}
