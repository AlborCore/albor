#include "Component328.h"

#include <cstdint>

struct Component328::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component327* m_child = nullptr;
};

Component328::Component328() :
    m_impl(new Impl) {}
Component328::~Component328() { delete m_impl; }

float Component328::color() const { return m_impl->m_color; }
float Component328::width() const { return m_impl->m_width; }
void Component328::set_color(float v) { m_impl->m_color = v; }
void Component328::set_opacity(float v) { m_impl->m_opacity = v; }
void Component328::set_width(float v) { m_impl->m_width = v; }
void Component328::set_height(float v) { m_impl->m_height = v; }
void Component328::set_child(Component327* child) { m_impl->m_child = child; }
void Component328::on_click() {}
void Component328::on_hover() {}
void Component328::on_focus() {}
