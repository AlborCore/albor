#include "Component475.h"

#include <cstdint>

struct Component475::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component474* m_child = nullptr;
};

Component475::Component475() : m_impl(new Impl) {}
Component475::~Component475() { delete m_impl; }

float Component475::color() const { return m_impl->m_color; }
float Component475::width() const { return m_impl->m_width; }
void Component475::set_color(float v) { m_impl->m_color = v; }
void Component475::set_opacity(float v) { m_impl->m_opacity = v; }
void Component475::set_width(float v) { m_impl->m_width = v; }
void Component475::set_height(float v) { m_impl->m_height = v; }
void Component475::set_child(Component474* child) { m_impl->m_child = child; }
void Component475::on_click() {}
void Component475::on_hover() {}
void Component475::on_focus() {}
