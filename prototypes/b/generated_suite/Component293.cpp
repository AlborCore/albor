#include "Component293.h"

#include <cstdint>

struct Component293::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component292* m_child = nullptr;
};

Component293::Component293() : m_impl(new Impl) {}
Component293::~Component293() { delete m_impl; }

float Component293::color() const { return m_impl->m_color; }
float Component293::width() const { return m_impl->m_width; }
void Component293::set_color(float v) { m_impl->m_color = v; }
void Component293::set_opacity(float v) { m_impl->m_opacity = v; }
void Component293::set_width(float v) { m_impl->m_width = v; }
void Component293::set_height(float v) { m_impl->m_height = v; }
void Component293::set_child(Component292* child) { m_impl->m_child = child; }
void Component293::on_click() {}
void Component293::on_hover() {}
void Component293::on_focus() {}
