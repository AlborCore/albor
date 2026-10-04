#include "Component379.h"

#include <cstdint>

struct Component379::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component378* m_child = nullptr;
};

Component379::Component379() : m_impl(new Impl) {}
Component379::~Component379() { delete m_impl; }

float Component379::color() const { return m_impl->m_color; }
float Component379::width() const { return m_impl->m_width; }
void Component379::set_color(float v) { m_impl->m_color = v; }
void Component379::set_opacity(float v) { m_impl->m_opacity = v; }
void Component379::set_width(float v) { m_impl->m_width = v; }
void Component379::set_height(float v) { m_impl->m_height = v; }
void Component379::set_child(Component378* child) { m_impl->m_child = child; }
void Component379::on_click() {}
void Component379::on_hover() {}
void Component379::on_focus() {}
