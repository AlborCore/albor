#include "Component254.h"

#include <cstdint>

struct Component254::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component253* m_child = nullptr;
};

Component254::Component254() :
    m_impl(new Impl) {}
Component254::~Component254() { delete m_impl; }

float Component254::color() const { return m_impl->m_color; }
float Component254::width() const { return m_impl->m_width; }
void Component254::set_color(float v) { m_impl->m_color = v; }
void Component254::set_opacity(float v) { m_impl->m_opacity = v; }
void Component254::set_width(float v) { m_impl->m_width = v; }
void Component254::set_height(float v) { m_impl->m_height = v; }
void Component254::set_child(Component253* child) { m_impl->m_child = child; }
void Component254::on_click() {}
void Component254::on_hover() {}
void Component254::on_focus() {}
