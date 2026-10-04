#include "Component213.h"

#include <cstdint>

struct Component213::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component212* m_child = nullptr;
};

Component213::Component213() : m_impl(new Impl) {}
Component213::~Component213() { delete m_impl; }

float Component213::color() const { return m_impl->m_color; }
float Component213::width() const { return m_impl->m_width; }
void Component213::set_color(float v) { m_impl->m_color = v; }
void Component213::set_opacity(float v) { m_impl->m_opacity = v; }
void Component213::set_width(float v) { m_impl->m_width = v; }
void Component213::set_height(float v) { m_impl->m_height = v; }
void Component213::set_child(Component212* child) { m_impl->m_child = child; }
void Component213::on_click() {}
void Component213::on_hover() {}
void Component213::on_focus() {}
