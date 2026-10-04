#include "Component191.h"

#include <cstdint>

struct Component191::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component190* m_child = nullptr;
};

Component191::Component191() : m_impl(new Impl) {}
Component191::~Component191() { delete m_impl; }

float Component191::color() const { return m_impl->m_color; }
float Component191::width() const { return m_impl->m_width; }
void Component191::set_color(float v) { m_impl->m_color = v; }
void Component191::set_opacity(float v) { m_impl->m_opacity = v; }
void Component191::set_width(float v) { m_impl->m_width = v; }
void Component191::set_height(float v) { m_impl->m_height = v; }
void Component191::set_child(Component190* child) { m_impl->m_child = child; }
void Component191::on_click() {}
void Component191::on_hover() {}
void Component191::on_focus() {}
