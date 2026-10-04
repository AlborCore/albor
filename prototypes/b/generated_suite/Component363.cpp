#include "Component363.h"

#include <cstdint>

struct Component363::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component362* m_child = nullptr;
};

Component363::Component363() : m_impl(new Impl) {}
Component363::~Component363() { delete m_impl; }

float Component363::color() const { return m_impl->m_color; }
float Component363::width() const { return m_impl->m_width; }
void Component363::set_color(float v) { m_impl->m_color = v; }
void Component363::set_opacity(float v) { m_impl->m_opacity = v; }
void Component363::set_width(float v) { m_impl->m_width = v; }
void Component363::set_height(float v) { m_impl->m_height = v; }
void Component363::set_child(Component362* child) { m_impl->m_child = child; }
void Component363::on_click() {}
void Component363::on_hover() {}
void Component363::on_focus() {}
