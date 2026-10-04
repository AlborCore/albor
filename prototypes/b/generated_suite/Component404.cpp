#include "Component404.h"

#include <cstdint>

struct Component404::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component403* m_child = nullptr;
};

Component404::Component404() : m_impl(new Impl) {}
Component404::~Component404() { delete m_impl; }

float Component404::color() const { return m_impl->m_color; }
float Component404::width() const { return m_impl->m_width; }
void Component404::set_color(float v) { m_impl->m_color = v; }
void Component404::set_opacity(float v) { m_impl->m_opacity = v; }
void Component404::set_width(float v) { m_impl->m_width = v; }
void Component404::set_height(float v) { m_impl->m_height = v; }
void Component404::set_child(Component403* child) { m_impl->m_child = child; }
void Component404::on_click() {}
void Component404::on_hover() {}
void Component404::on_focus() {}
