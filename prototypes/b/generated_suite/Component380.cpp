#include "Component380.h"

#include <cstdint>

struct Component380::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component379* m_child = nullptr;
};

Component380::Component380() :
    m_impl(new Impl) {}
Component380::~Component380() { delete m_impl; }

float Component380::color() const { return m_impl->m_color; }
float Component380::width() const { return m_impl->m_width; }
void Component380::set_color(float v) { m_impl->m_color = v; }
void Component380::set_opacity(float v) { m_impl->m_opacity = v; }
void Component380::set_width(float v) { m_impl->m_width = v; }
void Component380::set_height(float v) { m_impl->m_height = v; }
void Component380::set_child(Component379* child) { m_impl->m_child = child; }
void Component380::on_click() {}
void Component380::on_hover() {}
void Component380::on_focus() {}
