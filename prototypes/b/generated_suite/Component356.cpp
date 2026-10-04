#include "Component356.h"

#include <cstdint>

struct Component356::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component355* m_child = nullptr;
};

Component356::Component356() :
    m_impl(new Impl) {}
Component356::~Component356() { delete m_impl; }

float Component356::color() const { return m_impl->m_color; }
float Component356::width() const { return m_impl->m_width; }
void Component356::set_color(float v) { m_impl->m_color = v; }
void Component356::set_opacity(float v) { m_impl->m_opacity = v; }
void Component356::set_width(float v) { m_impl->m_width = v; }
void Component356::set_height(float v) { m_impl->m_height = v; }
void Component356::set_child(Component355* child) { m_impl->m_child = child; }
void Component356::on_click() {}
void Component356::on_hover() {}
void Component356::on_focus() {}
