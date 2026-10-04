#include "Component220.h"

#include <cstdint>

struct Component220::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component219* m_child = nullptr;
};

Component220::Component220() : m_impl(new Impl) {}
Component220::~Component220() { delete m_impl; }

float Component220::color() const { return m_impl->m_color; }
float Component220::width() const { return m_impl->m_width; }
void Component220::set_color(float v) { m_impl->m_color = v; }
void Component220::set_opacity(float v) { m_impl->m_opacity = v; }
void Component220::set_width(float v) { m_impl->m_width = v; }
void Component220::set_height(float v) { m_impl->m_height = v; }
void Component220::set_child(Component219* child) { m_impl->m_child = child; }
void Component220::on_click() {}
void Component220::on_hover() {}
void Component220::on_focus() {}
