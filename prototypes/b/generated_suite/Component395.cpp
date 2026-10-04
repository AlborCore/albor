#include "Component395.h"

#include <cstdint>

struct Component395::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component394* m_child = nullptr;
};

Component395::Component395() : m_impl(new Impl) {}
Component395::~Component395() { delete m_impl; }

float Component395::color() const { return m_impl->m_color; }
float Component395::width() const { return m_impl->m_width; }
void Component395::set_color(float v) { m_impl->m_color = v; }
void Component395::set_opacity(float v) { m_impl->m_opacity = v; }
void Component395::set_width(float v) { m_impl->m_width = v; }
void Component395::set_height(float v) { m_impl->m_height = v; }
void Component395::set_child(Component394* child) { m_impl->m_child = child; }
void Component395::on_click() {}
void Component395::on_hover() {}
void Component395::on_focus() {}
