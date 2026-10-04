#include "Component182.h"

#include <cstdint>

struct Component182::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component181* m_child = nullptr;
};

Component182::Component182() : m_impl(new Impl) {}
Component182::~Component182() { delete m_impl; }

float Component182::color() const { return m_impl->m_color; }
float Component182::width() const { return m_impl->m_width; }
void Component182::set_color(float v) { m_impl->m_color = v; }
void Component182::set_opacity(float v) { m_impl->m_opacity = v; }
void Component182::set_width(float v) { m_impl->m_width = v; }
void Component182::set_height(float v) { m_impl->m_height = v; }
void Component182::set_child(Component181* child) { m_impl->m_child = child; }
void Component182::on_click() {}
void Component182::on_hover() {}
void Component182::on_focus() {}
