#include "Component494.h"

#include <cstdint>

struct Component494::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component493* m_child = nullptr;
};

Component494::Component494() : m_impl(new Impl) {}
Component494::~Component494() { delete m_impl; }

float Component494::color() const { return m_impl->m_color; }
float Component494::width() const { return m_impl->m_width; }
void Component494::set_color(float v) { m_impl->m_color = v; }
void Component494::set_opacity(float v) { m_impl->m_opacity = v; }
void Component494::set_width(float v) { m_impl->m_width = v; }
void Component494::set_height(float v) { m_impl->m_height = v; }
void Component494::set_child(Component493* child) { m_impl->m_child = child; }
void Component494::on_click() {}
void Component494::on_hover() {}
void Component494::on_focus() {}
