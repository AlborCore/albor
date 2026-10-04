#include "Component332.h"

#include <cstdint>

struct Component332::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component331* m_child = nullptr;
};

Component332::Component332() : m_impl(new Impl) {}
Component332::~Component332() { delete m_impl; }

float Component332::color() const { return m_impl->m_color; }
float Component332::width() const { return m_impl->m_width; }
void Component332::set_color(float v) { m_impl->m_color = v; }
void Component332::set_opacity(float v) { m_impl->m_opacity = v; }
void Component332::set_width(float v) { m_impl->m_width = v; }
void Component332::set_height(float v) { m_impl->m_height = v; }
void Component332::set_child(Component331* child) { m_impl->m_child = child; }
void Component332::on_click() {}
void Component332::on_hover() {}
void Component332::on_focus() {}
