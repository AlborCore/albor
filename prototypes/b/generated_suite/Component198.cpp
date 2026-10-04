#include "Component198.h"

#include <cstdint>

struct Component198::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component197* m_child = nullptr;
};

Component198::Component198() :
    m_impl(new Impl) {}
Component198::~Component198() { delete m_impl; }

float Component198::color() const { return m_impl->m_color; }
float Component198::width() const { return m_impl->m_width; }
void Component198::set_color(float v) { m_impl->m_color = v; }
void Component198::set_opacity(float v) { m_impl->m_opacity = v; }
void Component198::set_width(float v) { m_impl->m_width = v; }
void Component198::set_height(float v) { m_impl->m_height = v; }
void Component198::set_child(Component197* child) { m_impl->m_child = child; }
void Component198::on_click() {}
void Component198::on_hover() {}
void Component198::on_focus() {}
