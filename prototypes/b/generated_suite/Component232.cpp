#include "Component232.h"

#include <cstdint>

struct Component232::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component231* m_child = nullptr;
};

Component232::Component232() :
    m_impl(new Impl) {}
Component232::~Component232() { delete m_impl; }

float Component232::color() const { return m_impl->m_color; }
float Component232::width() const { return m_impl->m_width; }
void Component232::set_color(float v) { m_impl->m_color = v; }
void Component232::set_opacity(float v) { m_impl->m_opacity = v; }
void Component232::set_width(float v) { m_impl->m_width = v; }
void Component232::set_height(float v) { m_impl->m_height = v; }
void Component232::set_child(Component231* child) { m_impl->m_child = child; }
void Component232::on_click() {}
void Component232::on_hover() {}
void Component232::on_focus() {}
