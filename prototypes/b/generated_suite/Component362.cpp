#include "Component362.h"

#include <cstdint>

struct Component362::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component361* m_child = nullptr;
};

Component362::Component362() : m_impl(new Impl) {}
Component362::~Component362() { delete m_impl; }

float Component362::color() const { return m_impl->m_color; }
float Component362::width() const { return m_impl->m_width; }
void Component362::set_color(float v) { m_impl->m_color = v; }
void Component362::set_opacity(float v) { m_impl->m_opacity = v; }
void Component362::set_width(float v) { m_impl->m_width = v; }
void Component362::set_height(float v) { m_impl->m_height = v; }
void Component362::set_child(Component361* child) { m_impl->m_child = child; }
void Component362::on_click() {}
void Component362::on_hover() {}
void Component362::on_focus() {}
