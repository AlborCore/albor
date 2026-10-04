#include "Component497.h"

#include <cstdint>

struct Component497::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component496* m_child = nullptr;
};

Component497::Component497() : m_impl(new Impl) {}
Component497::~Component497() { delete m_impl; }

float Component497::color() const { return m_impl->m_color; }
float Component497::width() const { return m_impl->m_width; }
void Component497::set_color(float v) { m_impl->m_color = v; }
void Component497::set_opacity(float v) { m_impl->m_opacity = v; }
void Component497::set_width(float v) { m_impl->m_width = v; }
void Component497::set_height(float v) { m_impl->m_height = v; }
void Component497::set_child(Component496* child) { m_impl->m_child = child; }
void Component497::on_click() {}
void Component497::on_hover() {}
void Component497::on_focus() {}
