#include "Component221.h"

#include <cstdint>

struct Component221::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component220* m_child = nullptr;
};

Component221::Component221() : m_impl(new Impl) {}
Component221::~Component221() { delete m_impl; }

float Component221::color() const { return m_impl->m_color; }
float Component221::width() const { return m_impl->m_width; }
void Component221::set_color(float v) { m_impl->m_color = v; }
void Component221::set_opacity(float v) { m_impl->m_opacity = v; }
void Component221::set_width(float v) { m_impl->m_width = v; }
void Component221::set_height(float v) { m_impl->m_height = v; }
void Component221::set_child(Component220* child) { m_impl->m_child = child; }
void Component221::on_click() {}
void Component221::on_hover() {}
void Component221::on_focus() {}
