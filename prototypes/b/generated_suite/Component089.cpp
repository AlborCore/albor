#include "Component089.h"

#include <cstdint>

struct Component089::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component088* m_child = nullptr;
};

Component089::Component089() :
    m_impl(new Impl) {}
Component089::~Component089() { delete m_impl; }

float Component089::color() const { return m_impl->m_color; }
float Component089::width() const { return m_impl->m_width; }
void Component089::set_color(float v) { m_impl->m_color = v; }
void Component089::set_opacity(float v) { m_impl->m_opacity = v; }
void Component089::set_width(float v) { m_impl->m_width = v; }
void Component089::set_height(float v) { m_impl->m_height = v; }
void Component089::set_child(Component088* child) { m_impl->m_child = child; }
void Component089::on_click() {}
void Component089::on_hover() {}
void Component089::on_focus() {}
