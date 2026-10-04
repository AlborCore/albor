#include "Component407.h"

#include <cstdint>

struct Component407::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component406* m_child = nullptr;
};

Component407::Component407() : m_impl(new Impl) {}
Component407::~Component407() { delete m_impl; }

float Component407::color() const { return m_impl->m_color; }
float Component407::width() const { return m_impl->m_width; }
void Component407::set_color(float v) { m_impl->m_color = v; }
void Component407::set_opacity(float v) { m_impl->m_opacity = v; }
void Component407::set_width(float v) { m_impl->m_width = v; }
void Component407::set_height(float v) { m_impl->m_height = v; }
void Component407::set_child(Component406* child) { m_impl->m_child = child; }
void Component407::on_click() {}
void Component407::on_hover() {}
void Component407::on_focus() {}
