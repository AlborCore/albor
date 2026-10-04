#include "Component031.h"

#include <cstdint>

struct Component031::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component030* m_child = nullptr;
};

Component031::Component031() : m_impl(new Impl) {}
Component031::~Component031() { delete m_impl; }

float Component031::color() const { return m_impl->m_color; }
float Component031::width() const { return m_impl->m_width; }
void Component031::set_color(float v) { m_impl->m_color = v; }
void Component031::set_opacity(float v) { m_impl->m_opacity = v; }
void Component031::set_width(float v) { m_impl->m_width = v; }
void Component031::set_height(float v) { m_impl->m_height = v; }
void Component031::set_child(Component030* child) { m_impl->m_child = child; }
void Component031::on_click() {}
void Component031::on_hover() {}
void Component031::on_focus() {}
