#include "Component396.h"

#include <cstdint>

struct Component396::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component395* m_child = nullptr;
};

Component396::Component396() : m_impl(new Impl) {}
Component396::~Component396() { delete m_impl; }

float Component396::color() const { return m_impl->m_color; }
float Component396::width() const { return m_impl->m_width; }
void Component396::set_color(float v) { m_impl->m_color = v; }
void Component396::set_opacity(float v) { m_impl->m_opacity = v; }
void Component396::set_width(float v) { m_impl->m_width = v; }
void Component396::set_height(float v) { m_impl->m_height = v; }
void Component396::set_child(Component395* child) { m_impl->m_child = child; }
void Component396::on_click() {}
void Component396::on_hover() {}
void Component396::on_focus() {}
