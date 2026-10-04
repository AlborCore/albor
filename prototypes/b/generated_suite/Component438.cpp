#include "Component438.h"

#include <cstdint>

struct Component438::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component437* m_child = nullptr;
};

Component438::Component438() : m_impl(new Impl) {}
Component438::~Component438() { delete m_impl; }

float Component438::color() const { return m_impl->m_color; }
float Component438::width() const { return m_impl->m_width; }
void Component438::set_color(float v) { m_impl->m_color = v; }
void Component438::set_opacity(float v) { m_impl->m_opacity = v; }
void Component438::set_width(float v) { m_impl->m_width = v; }
void Component438::set_height(float v) { m_impl->m_height = v; }
void Component438::set_child(Component437* child) { m_impl->m_child = child; }
void Component438::on_click() {}
void Component438::on_hover() {}
void Component438::on_focus() {}
