#include "Component054.h"

#include <cstdint>

struct Component054::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component053* m_child = nullptr;
};

Component054::Component054() : m_impl(new Impl) {}
Component054::~Component054() { delete m_impl; }

float Component054::color() const { return m_impl->m_color; }
float Component054::width() const { return m_impl->m_width; }
void Component054::set_color(float v) { m_impl->m_color = v; }
void Component054::set_opacity(float v) { m_impl->m_opacity = v; }
void Component054::set_width(float v) { m_impl->m_width = v; }
void Component054::set_height(float v) { m_impl->m_height = v; }
void Component054::set_child(Component053* child) { m_impl->m_child = child; }
void Component054::on_click() {}
void Component054::on_hover() {}
void Component054::on_focus() {}
