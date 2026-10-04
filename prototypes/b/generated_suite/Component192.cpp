#include "Component192.h"

#include <cstdint>

struct Component192::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component191* m_child = nullptr;
};

Component192::Component192() : m_impl(new Impl) {}
Component192::~Component192() { delete m_impl; }

float Component192::color() const { return m_impl->m_color; }
float Component192::width() const { return m_impl->m_width; }
void Component192::set_color(float v) { m_impl->m_color = v; }
void Component192::set_opacity(float v) { m_impl->m_opacity = v; }
void Component192::set_width(float v) { m_impl->m_width = v; }
void Component192::set_height(float v) { m_impl->m_height = v; }
void Component192::set_child(Component191* child) { m_impl->m_child = child; }
void Component192::on_click() {}
void Component192::on_hover() {}
void Component192::on_focus() {}
