#include "Component391.h"

#include <cstdint>

struct Component391::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component390* m_child = nullptr;
};

Component391::Component391() :
    m_impl(new Impl) {}
Component391::~Component391() { delete m_impl; }

float Component391::color() const { return m_impl->m_color; }
float Component391::width() const { return m_impl->m_width; }
void Component391::set_color(float v) { m_impl->m_color = v; }
void Component391::set_opacity(float v) { m_impl->m_opacity = v; }
void Component391::set_width(float v) { m_impl->m_width = v; }
void Component391::set_height(float v) { m_impl->m_height = v; }
void Component391::set_child(Component390* child) { m_impl->m_child = child; }
void Component391::on_click() {}
void Component391::on_hover() {}
void Component391::on_focus() {}
