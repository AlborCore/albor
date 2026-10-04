#include "Component277.h"

#include <cstdint>

struct Component277::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component276* m_child = nullptr;
};

Component277::Component277() : m_impl(new Impl) {}
Component277::~Component277() { delete m_impl; }

float Component277::color() const { return m_impl->m_color; }
float Component277::width() const { return m_impl->m_width; }
void Component277::set_color(float v) { m_impl->m_color = v; }
void Component277::set_opacity(float v) { m_impl->m_opacity = v; }
void Component277::set_width(float v) { m_impl->m_width = v; }
void Component277::set_height(float v) { m_impl->m_height = v; }
void Component277::set_child(Component276* child) { m_impl->m_child = child; }
void Component277::on_click() {}
void Component277::on_hover() {}
void Component277::on_focus() {}
