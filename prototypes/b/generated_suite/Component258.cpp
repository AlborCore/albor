#include "Component258.h"

#include <cstdint>

struct Component258::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component257* m_child = nullptr;
};

Component258::Component258() : m_impl(new Impl) {}
Component258::~Component258() { delete m_impl; }

float Component258::color() const { return m_impl->m_color; }
float Component258::width() const { return m_impl->m_width; }
void Component258::set_color(float v) { m_impl->m_color = v; }
void Component258::set_opacity(float v) { m_impl->m_opacity = v; }
void Component258::set_width(float v) { m_impl->m_width = v; }
void Component258::set_height(float v) { m_impl->m_height = v; }
void Component258::set_child(Component257* child) { m_impl->m_child = child; }
void Component258::on_click() {}
void Component258::on_hover() {}
void Component258::on_focus() {}
