#include "Component171.h"

#include <cstdint>

struct Component171::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component170* m_child = nullptr;
};

Component171::Component171() :
    m_impl(new Impl) {}
Component171::~Component171() { delete m_impl; }

float Component171::color() const { return m_impl->m_color; }
float Component171::width() const { return m_impl->m_width; }
void Component171::set_color(float v) { m_impl->m_color = v; }
void Component171::set_opacity(float v) { m_impl->m_opacity = v; }
void Component171::set_width(float v) { m_impl->m_width = v; }
void Component171::set_height(float v) { m_impl->m_height = v; }
void Component171::set_child(Component170* child) { m_impl->m_child = child; }
void Component171::on_click() {}
void Component171::on_hover() {}
void Component171::on_focus() {}
