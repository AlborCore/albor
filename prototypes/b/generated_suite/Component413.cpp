#include "Component413.h"

#include <cstdint>

struct Component413::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component412* m_child = nullptr;
};

Component413::Component413() :
    m_impl(new Impl) {}
Component413::~Component413() { delete m_impl; }

float Component413::color() const { return m_impl->m_color; }
float Component413::width() const { return m_impl->m_width; }
void Component413::set_color(float v) { m_impl->m_color = v; }
void Component413::set_opacity(float v) { m_impl->m_opacity = v; }
void Component413::set_width(float v) { m_impl->m_width = v; }
void Component413::set_height(float v) { m_impl->m_height = v; }
void Component413::set_child(Component412* child) { m_impl->m_child = child; }
void Component413::on_click() {}
void Component413::on_hover() {}
void Component413::on_focus() {}
