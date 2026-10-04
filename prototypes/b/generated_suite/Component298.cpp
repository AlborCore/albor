#include "Component298.h"

#include <cstdint>

struct Component298::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component297* m_child = nullptr;
};

Component298::Component298() : m_impl(new Impl) {}
Component298::~Component298() { delete m_impl; }

float Component298::color() const { return m_impl->m_color; }
float Component298::width() const { return m_impl->m_width; }
void Component298::set_color(float v) { m_impl->m_color = v; }
void Component298::set_opacity(float v) { m_impl->m_opacity = v; }
void Component298::set_width(float v) { m_impl->m_width = v; }
void Component298::set_height(float v) { m_impl->m_height = v; }
void Component298::set_child(Component297* child) { m_impl->m_child = child; }
void Component298::on_click() {}
void Component298::on_hover() {}
void Component298::on_focus() {}
