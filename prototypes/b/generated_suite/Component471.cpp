#include "Component471.h"

#include <cstdint>

struct Component471::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component470* m_child = nullptr;
};

Component471::Component471() : m_impl(new Impl) {}
Component471::~Component471() { delete m_impl; }

float Component471::color() const { return m_impl->m_color; }
float Component471::width() const { return m_impl->m_width; }
void Component471::set_color(float v) { m_impl->m_color = v; }
void Component471::set_opacity(float v) { m_impl->m_opacity = v; }
void Component471::set_width(float v) { m_impl->m_width = v; }
void Component471::set_height(float v) { m_impl->m_height = v; }
void Component471::set_child(Component470* child) { m_impl->m_child = child; }
void Component471::on_click() {}
void Component471::on_hover() {}
void Component471::on_focus() {}
