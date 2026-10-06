#include "Component139.h"

#include <cstdint>

struct Component139::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component138* m_child = nullptr;
};

Component139::Component139() :
    m_impl(new Impl) {}
Component139::~Component139() { delete m_impl; }

float Component139::color() const { return m_impl->m_color; }
float Component139::width() const { return m_impl->m_width; }
void Component139::set_color(float v) { m_impl->m_color = v; }
void Component139::set_opacity(float v) { m_impl->m_opacity = v; }
void Component139::set_width(float v) { m_impl->m_width = v; }
void Component139::set_height(float v) { m_impl->m_height = v; }
void Component139::set_child(Component138* child) { m_impl->m_child = child; }
void Component139::on_click() {}
void Component139::on_hover() {}
void Component139::on_focus() {}
