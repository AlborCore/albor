#include "Component087.h"

#include <cstdint>

struct Component087::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component086* m_child = nullptr;
};

Component087::Component087() : m_impl(new Impl) {}
Component087::~Component087() { delete m_impl; }

float Component087::color() const { return m_impl->m_color; }
float Component087::width() const { return m_impl->m_width; }
void Component087::set_color(float v) { m_impl->m_color = v; }
void Component087::set_opacity(float v) { m_impl->m_opacity = v; }
void Component087::set_width(float v) { m_impl->m_width = v; }
void Component087::set_height(float v) { m_impl->m_height = v; }
void Component087::set_child(Component086* child) { m_impl->m_child = child; }
void Component087::on_click() {}
void Component087::on_hover() {}
void Component087::on_focus() {}
