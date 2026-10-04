#include "Component323.h"

#include <cstdint>

struct Component323::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component322* m_child = nullptr;
};

Component323::Component323() :
    m_impl(new Impl) {}
Component323::~Component323() { delete m_impl; }

float Component323::color() const { return m_impl->m_color; }
float Component323::width() const { return m_impl->m_width; }
void Component323::set_color(float v) { m_impl->m_color = v; }
void Component323::set_opacity(float v) { m_impl->m_opacity = v; }
void Component323::set_width(float v) { m_impl->m_width = v; }
void Component323::set_height(float v) { m_impl->m_height = v; }
void Component323::set_child(Component322* child) { m_impl->m_child = child; }
void Component323::on_click() {}
void Component323::on_hover() {}
void Component323::on_focus() {}
