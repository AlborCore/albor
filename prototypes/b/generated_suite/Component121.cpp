#include "Component121.h"

#include <cstdint>

struct Component121::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component120* m_child = nullptr;
};

Component121::Component121() : m_impl(new Impl) {}
Component121::~Component121() { delete m_impl; }

float Component121::color() const { return m_impl->m_color; }
float Component121::width() const { return m_impl->m_width; }
void Component121::set_color(float v) { m_impl->m_color = v; }
void Component121::set_opacity(float v) { m_impl->m_opacity = v; }
void Component121::set_width(float v) { m_impl->m_width = v; }
void Component121::set_height(float v) { m_impl->m_height = v; }
void Component121::set_child(Component120* child) { m_impl->m_child = child; }
void Component121::on_click() {}
void Component121::on_hover() {}
void Component121::on_focus() {}
