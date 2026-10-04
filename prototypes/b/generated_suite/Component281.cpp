#include "Component281.h"

#include <cstdint>

struct Component281::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component280* m_child = nullptr;
};

Component281::Component281() : m_impl(new Impl) {}
Component281::~Component281() { delete m_impl; }

float Component281::color() const { return m_impl->m_color; }
float Component281::width() const { return m_impl->m_width; }
void Component281::set_color(float v) { m_impl->m_color = v; }
void Component281::set_opacity(float v) { m_impl->m_opacity = v; }
void Component281::set_width(float v) { m_impl->m_width = v; }
void Component281::set_height(float v) { m_impl->m_height = v; }
void Component281::set_child(Component280* child) { m_impl->m_child = child; }
void Component281::on_click() {}
void Component281::on_hover() {}
void Component281::on_focus() {}
