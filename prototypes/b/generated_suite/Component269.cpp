#include "Component269.h"

#include <cstdint>

struct Component269::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component268* m_child = nullptr;
};

Component269::Component269() :
    m_impl(new Impl) {}
Component269::~Component269() { delete m_impl; }

float Component269::color() const { return m_impl->m_color; }
float Component269::width() const { return m_impl->m_width; }
void Component269::set_color(float v) { m_impl->m_color = v; }
void Component269::set_opacity(float v) { m_impl->m_opacity = v; }
void Component269::set_width(float v) { m_impl->m_width = v; }
void Component269::set_height(float v) { m_impl->m_height = v; }
void Component269::set_child(Component268* child) { m_impl->m_child = child; }
void Component269::on_click() {}
void Component269::on_hover() {}
void Component269::on_focus() {}
