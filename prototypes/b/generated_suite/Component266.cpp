#include "Component266.h"

#include <cstdint>

struct Component266::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component265* m_child = nullptr;
};

Component266::Component266() : m_impl(new Impl) {}
Component266::~Component266() { delete m_impl; }

float Component266::color() const { return m_impl->m_color; }
float Component266::width() const { return m_impl->m_width; }
void Component266::set_color(float v) { m_impl->m_color = v; }
void Component266::set_opacity(float v) { m_impl->m_opacity = v; }
void Component266::set_width(float v) { m_impl->m_width = v; }
void Component266::set_height(float v) { m_impl->m_height = v; }
void Component266::set_child(Component265* child) { m_impl->m_child = child; }
void Component266::on_click() {}
void Component266::on_hover() {}
void Component266::on_focus() {}
