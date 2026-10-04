#include "Component005.h"

#include <cstdint>

struct Component005::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component004* m_child = nullptr;
};

Component005::Component005() :
    m_impl(new Impl) {}
Component005::~Component005() { delete m_impl; }

float Component005::color() const { return m_impl->m_color; }
float Component005::width() const { return m_impl->m_width; }
void Component005::set_color(float v) { m_impl->m_color = v; }
void Component005::set_opacity(float v) { m_impl->m_opacity = v; }
void Component005::set_width(float v) { m_impl->m_width = v; }
void Component005::set_height(float v) { m_impl->m_height = v; }
void Component005::set_child(Component004* child) { m_impl->m_child = child; }
void Component005::on_click() {}
void Component005::on_hover() {}
void Component005::on_focus() {}
