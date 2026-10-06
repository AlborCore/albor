#include "Component131.h"

#include <cstdint>

struct Component131::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component130* m_child = nullptr;
};

Component131::Component131() :
    m_impl(new Impl) {}
Component131::~Component131() { delete m_impl; }

float Component131::color() const { return m_impl->m_color; }
float Component131::width() const { return m_impl->m_width; }
void Component131::set_color(float v) { m_impl->m_color = v; }
void Component131::set_opacity(float v) { m_impl->m_opacity = v; }
void Component131::set_width(float v) { m_impl->m_width = v; }
void Component131::set_height(float v) { m_impl->m_height = v; }
void Component131::set_child(Component130* child) { m_impl->m_child = child; }
void Component131::on_click() {}
void Component131::on_hover() {}
void Component131::on_focus() {}
