#include "Component357.h"

#include <cstdint>

struct Component357::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component356* m_child = nullptr;
};

Component357::Component357() : m_impl(new Impl) {}
Component357::~Component357() { delete m_impl; }

float Component357::color() const { return m_impl->m_color; }
float Component357::width() const { return m_impl->m_width; }
void Component357::set_color(float v) { m_impl->m_color = v; }
void Component357::set_opacity(float v) { m_impl->m_opacity = v; }
void Component357::set_width(float v) { m_impl->m_width = v; }
void Component357::set_height(float v) { m_impl->m_height = v; }
void Component357::set_child(Component356* child) { m_impl->m_child = child; }
void Component357::on_click() {}
void Component357::on_hover() {}
void Component357::on_focus() {}
