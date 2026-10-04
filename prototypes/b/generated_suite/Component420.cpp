#include "Component420.h"

#include <cstdint>

struct Component420::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component419* m_child = nullptr;
};

Component420::Component420() : m_impl(new Impl) {}
Component420::~Component420() { delete m_impl; }

float Component420::color() const { return m_impl->m_color; }
float Component420::width() const { return m_impl->m_width; }
void Component420::set_color(float v) { m_impl->m_color = v; }
void Component420::set_opacity(float v) { m_impl->m_opacity = v; }
void Component420::set_width(float v) { m_impl->m_width = v; }
void Component420::set_height(float v) { m_impl->m_height = v; }
void Component420::set_child(Component419* child) { m_impl->m_child = child; }
void Component420::on_click() {}
void Component420::on_hover() {}
void Component420::on_focus() {}
