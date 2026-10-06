#include "Component457.h"

#include <cstdint>

struct Component457::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component456* m_child = nullptr;
};

Component457::Component457() :
    m_impl(new Impl) {}
Component457::~Component457() { delete m_impl; }

float Component457::color() const { return m_impl->m_color; }
float Component457::width() const { return m_impl->m_width; }
void Component457::set_color(float v) { m_impl->m_color = v; }
void Component457::set_opacity(float v) { m_impl->m_opacity = v; }
void Component457::set_width(float v) { m_impl->m_width = v; }
void Component457::set_height(float v) { m_impl->m_height = v; }
void Component457::set_child(Component456* child) { m_impl->m_child = child; }
void Component457::on_click() {}
void Component457::on_hover() {}
void Component457::on_focus() {}
