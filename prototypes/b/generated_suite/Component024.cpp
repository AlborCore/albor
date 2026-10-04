#include "Component024.h"

#include <cstdint>

struct Component024::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component023* m_child = nullptr;
};

Component024::Component024() : m_impl(new Impl) {}
Component024::~Component024() { delete m_impl; }

float Component024::color() const { return m_impl->m_color; }
float Component024::width() const { return m_impl->m_width; }
void Component024::set_color(float v) { m_impl->m_color = v; }
void Component024::set_opacity(float v) { m_impl->m_opacity = v; }
void Component024::set_width(float v) { m_impl->m_width = v; }
void Component024::set_height(float v) { m_impl->m_height = v; }
void Component024::set_child(Component023* child) { m_impl->m_child = child; }
void Component024::on_click() {}
void Component024::on_hover() {}
void Component024::on_focus() {}
