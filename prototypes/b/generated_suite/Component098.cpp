#include "Component098.h"

#include <cstdint>

struct Component098::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component097* m_child = nullptr;
};

Component098::Component098() :
    m_impl(new Impl) {}
Component098::~Component098() { delete m_impl; }

float Component098::color() const { return m_impl->m_color; }
float Component098::width() const { return m_impl->m_width; }
void Component098::set_color(float v) { m_impl->m_color = v; }
void Component098::set_opacity(float v) { m_impl->m_opacity = v; }
void Component098::set_width(float v) { m_impl->m_width = v; }
void Component098::set_height(float v) { m_impl->m_height = v; }
void Component098::set_child(Component097* child) { m_impl->m_child = child; }
void Component098::on_click() {}
void Component098::on_hover() {}
void Component098::on_focus() {}
