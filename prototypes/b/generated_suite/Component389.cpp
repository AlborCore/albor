#include "Component389.h"

#include <cstdint>

struct Component389::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component388* m_child = nullptr;
};

Component389::Component389() :
    m_impl(new Impl) {}
Component389::~Component389() { delete m_impl; }

float Component389::color() const { return m_impl->m_color; }
float Component389::width() const { return m_impl->m_width; }
void Component389::set_color(float v) { m_impl->m_color = v; }
void Component389::set_opacity(float v) { m_impl->m_opacity = v; }
void Component389::set_width(float v) { m_impl->m_width = v; }
void Component389::set_height(float v) { m_impl->m_height = v; }
void Component389::set_child(Component388* child) { m_impl->m_child = child; }
void Component389::on_click() {}
void Component389::on_hover() {}
void Component389::on_focus() {}
