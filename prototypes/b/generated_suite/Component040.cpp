#include "Component040.h"

#include <cstdint>

struct Component040::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component039* m_child = nullptr;
};

Component040::Component040() : m_impl(new Impl) {}
Component040::~Component040() { delete m_impl; }

float Component040::color() const { return m_impl->m_color; }
float Component040::width() const { return m_impl->m_width; }
void Component040::set_color(float v) { m_impl->m_color = v; }
void Component040::set_opacity(float v) { m_impl->m_opacity = v; }
void Component040::set_width(float v) { m_impl->m_width = v; }
void Component040::set_height(float v) { m_impl->m_height = v; }
void Component040::set_child(Component039* child) { m_impl->m_child = child; }
void Component040::on_click() {}
void Component040::on_hover() {}
void Component040::on_focus() {}
