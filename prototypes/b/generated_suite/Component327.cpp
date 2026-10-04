#include "Component327.h"

#include <cstdint>

struct Component327::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component326* m_child = nullptr;
};

Component327::Component327() : m_impl(new Impl) {}
Component327::~Component327() { delete m_impl; }

float Component327::color() const { return m_impl->m_color; }
float Component327::width() const { return m_impl->m_width; }
void Component327::set_color(float v) { m_impl->m_color = v; }
void Component327::set_opacity(float v) { m_impl->m_opacity = v; }
void Component327::set_width(float v) { m_impl->m_width = v; }
void Component327::set_height(float v) { m_impl->m_height = v; }
void Component327::set_child(Component326* child) { m_impl->m_child = child; }
void Component327::on_click() {}
void Component327::on_hover() {}
void Component327::on_focus() {}
