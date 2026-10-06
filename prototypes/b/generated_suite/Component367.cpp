#include "Component367.h"

#include <cstdint>

struct Component367::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component366* m_child = nullptr;
};

Component367::Component367() :
    m_impl(new Impl) {}
Component367::~Component367() { delete m_impl; }

float Component367::color() const { return m_impl->m_color; }
float Component367::width() const { return m_impl->m_width; }
void Component367::set_color(float v) { m_impl->m_color = v; }
void Component367::set_opacity(float v) { m_impl->m_opacity = v; }
void Component367::set_width(float v) { m_impl->m_width = v; }
void Component367::set_height(float v) { m_impl->m_height = v; }
void Component367::set_child(Component366* child) { m_impl->m_child = child; }
void Component367::on_click() {}
void Component367::on_hover() {}
void Component367::on_focus() {}
