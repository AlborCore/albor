#include "Component331.h"

#include <cstdint>

struct Component331::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component330* m_child = nullptr;
};

Component331::Component331() :
    m_impl(new Impl) {}
Component331::~Component331() { delete m_impl; }

float Component331::color() const { return m_impl->m_color; }
float Component331::width() const { return m_impl->m_width; }
void Component331::set_color(float v) { m_impl->m_color = v; }
void Component331::set_opacity(float v) { m_impl->m_opacity = v; }
void Component331::set_width(float v) { m_impl->m_width = v; }
void Component331::set_height(float v) { m_impl->m_height = v; }
void Component331::set_child(Component330* child) { m_impl->m_child = child; }
void Component331::on_click() {}
void Component331::on_hover() {}
void Component331::on_focus() {}
