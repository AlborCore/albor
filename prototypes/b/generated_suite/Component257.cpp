#include "Component257.h"

#include <cstdint>

struct Component257::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component256* m_child = nullptr;
};

Component257::Component257() :
    m_impl(new Impl) {}
Component257::~Component257() { delete m_impl; }

float Component257::color() const { return m_impl->m_color; }
float Component257::width() const { return m_impl->m_width; }
void Component257::set_color(float v) { m_impl->m_color = v; }
void Component257::set_opacity(float v) { m_impl->m_opacity = v; }
void Component257::set_width(float v) { m_impl->m_width = v; }
void Component257::set_height(float v) { m_impl->m_height = v; }
void Component257::set_child(Component256* child) { m_impl->m_child = child; }
void Component257::on_click() {}
void Component257::on_hover() {}
void Component257::on_focus() {}
