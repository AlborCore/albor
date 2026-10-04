#include "Component322.h"

#include <cstdint>

struct Component322::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component321* m_child = nullptr;
};

Component322::Component322() :
    m_impl(new Impl) {}
Component322::~Component322() { delete m_impl; }

float Component322::color() const { return m_impl->m_color; }
float Component322::width() const { return m_impl->m_width; }
void Component322::set_color(float v) { m_impl->m_color = v; }
void Component322::set_opacity(float v) { m_impl->m_opacity = v; }
void Component322::set_width(float v) { m_impl->m_width = v; }
void Component322::set_height(float v) { m_impl->m_height = v; }
void Component322::set_child(Component321* child) { m_impl->m_child = child; }
void Component322::on_click() {}
void Component322::on_hover() {}
void Component322::on_focus() {}
