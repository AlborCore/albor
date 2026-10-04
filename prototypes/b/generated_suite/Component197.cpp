#include "Component197.h"

#include <cstdint>

struct Component197::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component196* m_child = nullptr;
};

Component197::Component197() : m_impl(new Impl) {}
Component197::~Component197() { delete m_impl; }

float Component197::color() const { return m_impl->m_color; }
float Component197::width() const { return m_impl->m_width; }
void Component197::set_color(float v) { m_impl->m_color = v; }
void Component197::set_opacity(float v) { m_impl->m_opacity = v; }
void Component197::set_width(float v) { m_impl->m_width = v; }
void Component197::set_height(float v) { m_impl->m_height = v; }
void Component197::set_child(Component196* child) { m_impl->m_child = child; }
void Component197::on_click() {}
void Component197::on_hover() {}
void Component197::on_focus() {}
