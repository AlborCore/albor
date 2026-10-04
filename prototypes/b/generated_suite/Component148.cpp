#include "Component148.h"

#include <cstdint>

struct Component148::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component147* m_child = nullptr;
};

Component148::Component148() : m_impl(new Impl) {}
Component148::~Component148() { delete m_impl; }

float Component148::color() const { return m_impl->m_color; }
float Component148::width() const { return m_impl->m_width; }
void Component148::set_color(float v) { m_impl->m_color = v; }
void Component148::set_opacity(float v) { m_impl->m_opacity = v; }
void Component148::set_width(float v) { m_impl->m_width = v; }
void Component148::set_height(float v) { m_impl->m_height = v; }
void Component148::set_child(Component147* child) { m_impl->m_child = child; }
void Component148::on_click() {}
void Component148::on_hover() {}
void Component148::on_focus() {}
