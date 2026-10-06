#include "Component397.h"

#include <cstdint>

struct Component397::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component396* m_child = nullptr;
};

Component397::Component397() :
    m_impl(new Impl) {}
Component397::~Component397() { delete m_impl; }

float Component397::color() const { return m_impl->m_color; }
float Component397::width() const { return m_impl->m_width; }
void Component397::set_color(float v) { m_impl->m_color = v; }
void Component397::set_opacity(float v) { m_impl->m_opacity = v; }
void Component397::set_width(float v) { m_impl->m_width = v; }
void Component397::set_height(float v) { m_impl->m_height = v; }
void Component397::set_child(Component396* child) { m_impl->m_child = child; }
void Component397::on_click() {}
void Component397::on_hover() {}
void Component397::on_focus() {}
