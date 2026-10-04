#include "Component271.h"

#include <cstdint>

struct Component271::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component270* m_child = nullptr;
};

Component271::Component271() : m_impl(new Impl) {}
Component271::~Component271() { delete m_impl; }

float Component271::color() const { return m_impl->m_color; }
float Component271::width() const { return m_impl->m_width; }
void Component271::set_color(float v) { m_impl->m_color = v; }
void Component271::set_opacity(float v) { m_impl->m_opacity = v; }
void Component271::set_width(float v) { m_impl->m_width = v; }
void Component271::set_height(float v) { m_impl->m_height = v; }
void Component271::set_child(Component270* child) { m_impl->m_child = child; }
void Component271::on_click() {}
void Component271::on_hover() {}
void Component271::on_focus() {}
