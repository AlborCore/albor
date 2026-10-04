#include "Component434.h"

#include <cstdint>

struct Component434::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component433* m_child = nullptr;
};

Component434::Component434() : m_impl(new Impl) {}
Component434::~Component434() { delete m_impl; }

float Component434::color() const { return m_impl->m_color; }
float Component434::width() const { return m_impl->m_width; }
void Component434::set_color(float v) { m_impl->m_color = v; }
void Component434::set_opacity(float v) { m_impl->m_opacity = v; }
void Component434::set_width(float v) { m_impl->m_width = v; }
void Component434::set_height(float v) { m_impl->m_height = v; }
void Component434::set_child(Component433* child) { m_impl->m_child = child; }
void Component434::on_click() {}
void Component434::on_hover() {}
void Component434::on_focus() {}
