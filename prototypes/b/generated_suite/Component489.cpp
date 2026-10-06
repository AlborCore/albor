#include "Component489.h"

#include <cstdint>

struct Component489::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component488* m_child = nullptr;
};

Component489::Component489() :
    m_impl(new Impl) {}
Component489::~Component489() { delete m_impl; }

float Component489::color() const { return m_impl->m_color; }
float Component489::width() const { return m_impl->m_width; }
void Component489::set_color(float v) { m_impl->m_color = v; }
void Component489::set_opacity(float v) { m_impl->m_opacity = v; }
void Component489::set_width(float v) { m_impl->m_width = v; }
void Component489::set_height(float v) { m_impl->m_height = v; }
void Component489::set_child(Component488* child) { m_impl->m_child = child; }
void Component489::on_click() {}
void Component489::on_hover() {}
void Component489::on_focus() {}
