#include "Component123.h"

#include <cstdint>

struct Component123::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component122* m_child = nullptr;
};

Component123::Component123() :
    m_impl(new Impl) {}
Component123::~Component123() { delete m_impl; }

float Component123::color() const { return m_impl->m_color; }
float Component123::width() const { return m_impl->m_width; }
void Component123::set_color(float v) { m_impl->m_color = v; }
void Component123::set_opacity(float v) { m_impl->m_opacity = v; }
void Component123::set_width(float v) { m_impl->m_width = v; }
void Component123::set_height(float v) { m_impl->m_height = v; }
void Component123::set_child(Component122* child) { m_impl->m_child = child; }
void Component123::on_click() {}
void Component123::on_hover() {}
void Component123::on_focus() {}
