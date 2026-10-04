#include "Component203.h"

#include <cstdint>

struct Component203::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component202* m_child = nullptr;
};

Component203::Component203() :
    m_impl(new Impl) {}
Component203::~Component203() { delete m_impl; }

float Component203::color() const { return m_impl->m_color; }
float Component203::width() const { return m_impl->m_width; }
void Component203::set_color(float v) { m_impl->m_color = v; }
void Component203::set_opacity(float v) { m_impl->m_opacity = v; }
void Component203::set_width(float v) { m_impl->m_width = v; }
void Component203::set_height(float v) { m_impl->m_height = v; }
void Component203::set_child(Component202* child) { m_impl->m_child = child; }
void Component203::on_click() {}
void Component203::on_hover() {}
void Component203::on_focus() {}
