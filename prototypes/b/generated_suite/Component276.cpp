#include "Component276.h"

#include <cstdint>

struct Component276::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component275* m_child = nullptr;
};

Component276::Component276() : m_impl(new Impl) {}
Component276::~Component276() { delete m_impl; }

float Component276::color() const { return m_impl->m_color; }
float Component276::width() const { return m_impl->m_width; }
void Component276::set_color(float v) { m_impl->m_color = v; }
void Component276::set_opacity(float v) { m_impl->m_opacity = v; }
void Component276::set_width(float v) { m_impl->m_width = v; }
void Component276::set_height(float v) { m_impl->m_height = v; }
void Component276::set_child(Component275* child) { m_impl->m_child = child; }
void Component276::on_click() {}
void Component276::on_hover() {}
void Component276::on_focus() {}
