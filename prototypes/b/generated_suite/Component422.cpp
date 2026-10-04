#include "Component422.h"

#include <cstdint>

struct Component422::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component421* m_child = nullptr;
};

Component422::Component422() :
    m_impl(new Impl) {}
Component422::~Component422() { delete m_impl; }

float Component422::color() const { return m_impl->m_color; }
float Component422::width() const { return m_impl->m_width; }
void Component422::set_color(float v) { m_impl->m_color = v; }
void Component422::set_opacity(float v) { m_impl->m_opacity = v; }
void Component422::set_width(float v) { m_impl->m_width = v; }
void Component422::set_height(float v) { m_impl->m_height = v; }
void Component422::set_child(Component421* child) { m_impl->m_child = child; }
void Component422::on_click() {}
void Component422::on_hover() {}
void Component422::on_focus() {}
