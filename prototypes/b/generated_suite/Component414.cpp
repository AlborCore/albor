#include "Component414.h"

#include <cstdint>

struct Component414::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component413* m_child = nullptr;
};

Component414::Component414() :
    m_impl(new Impl) {}
Component414::~Component414() { delete m_impl; }

float Component414::color() const { return m_impl->m_color; }
float Component414::width() const { return m_impl->m_width; }
void Component414::set_color(float v) { m_impl->m_color = v; }
void Component414::set_opacity(float v) { m_impl->m_opacity = v; }
void Component414::set_width(float v) { m_impl->m_width = v; }
void Component414::set_height(float v) { m_impl->m_height = v; }
void Component414::set_child(Component413* child) { m_impl->m_child = child; }
void Component414::on_click() {}
void Component414::on_hover() {}
void Component414::on_focus() {}
