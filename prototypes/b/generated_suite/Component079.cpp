#include "Component079.h"

#include <cstdint>

struct Component079::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component078* m_child = nullptr;
};

Component079::Component079() : m_impl(new Impl) {}
Component079::~Component079() { delete m_impl; }

float Component079::color() const { return m_impl->m_color; }
float Component079::width() const { return m_impl->m_width; }
void Component079::set_color(float v) { m_impl->m_color = v; }
void Component079::set_opacity(float v) { m_impl->m_opacity = v; }
void Component079::set_width(float v) { m_impl->m_width = v; }
void Component079::set_height(float v) { m_impl->m_height = v; }
void Component079::set_child(Component078* child) { m_impl->m_child = child; }
void Component079::on_click() {}
void Component079::on_hover() {}
void Component079::on_focus() {}
