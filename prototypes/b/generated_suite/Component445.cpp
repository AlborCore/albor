#include "Component445.h"

#include <cstdint>

struct Component445::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component444* m_child = nullptr;
};

Component445::Component445() :
    m_impl(new Impl) {}
Component445::~Component445() { delete m_impl; }

float Component445::color() const { return m_impl->m_color; }
float Component445::width() const { return m_impl->m_width; }
void Component445::set_color(float v) { m_impl->m_color = v; }
void Component445::set_opacity(float v) { m_impl->m_opacity = v; }
void Component445::set_width(float v) { m_impl->m_width = v; }
void Component445::set_height(float v) { m_impl->m_height = v; }
void Component445::set_child(Component444* child) { m_impl->m_child = child; }
void Component445::on_click() {}
void Component445::on_hover() {}
void Component445::on_focus() {}
