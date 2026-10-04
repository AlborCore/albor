#include "Component291.h"

#include <cstdint>

struct Component291::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component290* m_child = nullptr;
};

Component291::Component291() : m_impl(new Impl) {}
Component291::~Component291() { delete m_impl; }

float Component291::color() const { return m_impl->m_color; }
float Component291::width() const { return m_impl->m_width; }
void Component291::set_color(float v) { m_impl->m_color = v; }
void Component291::set_opacity(float v) { m_impl->m_opacity = v; }
void Component291::set_width(float v) { m_impl->m_width = v; }
void Component291::set_height(float v) { m_impl->m_height = v; }
void Component291::set_child(Component290* child) { m_impl->m_child = child; }
void Component291::on_click() {}
void Component291::on_hover() {}
void Component291::on_focus() {}
