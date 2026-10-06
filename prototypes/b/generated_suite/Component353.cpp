#include "Component353.h"

#include <cstdint>

struct Component353::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component352* m_child = nullptr;
};

Component353::Component353() :
    m_impl(new Impl) {}
Component353::~Component353() { delete m_impl; }

float Component353::color() const { return m_impl->m_color; }
float Component353::width() const { return m_impl->m_width; }
void Component353::set_color(float v) { m_impl->m_color = v; }
void Component353::set_opacity(float v) { m_impl->m_opacity = v; }
void Component353::set_width(float v) { m_impl->m_width = v; }
void Component353::set_height(float v) { m_impl->m_height = v; }
void Component353::set_child(Component352* child) { m_impl->m_child = child; }
void Component353::on_click() {}
void Component353::on_hover() {}
void Component353::on_focus() {}
