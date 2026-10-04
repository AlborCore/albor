#include "Component119.h"

#include <cstdint>

struct Component119::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component118* m_child = nullptr;
};

Component119::Component119() : m_impl(new Impl) {}
Component119::~Component119() { delete m_impl; }

float Component119::color() const { return m_impl->m_color; }
float Component119::width() const { return m_impl->m_width; }
void Component119::set_color(float v) { m_impl->m_color = v; }
void Component119::set_opacity(float v) { m_impl->m_opacity = v; }
void Component119::set_width(float v) { m_impl->m_width = v; }
void Component119::set_height(float v) { m_impl->m_height = v; }
void Component119::set_child(Component118* child) { m_impl->m_child = child; }
void Component119::on_click() {}
void Component119::on_hover() {}
void Component119::on_focus() {}
