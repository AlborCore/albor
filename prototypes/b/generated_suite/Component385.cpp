#include "Component385.h"

#include <cstdint>

struct Component385::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component384* m_child = nullptr;
};

Component385::Component385() : m_impl(new Impl) {}
Component385::~Component385() { delete m_impl; }

float Component385::color() const { return m_impl->m_color; }
float Component385::width() const { return m_impl->m_width; }
void Component385::set_color(float v) { m_impl->m_color = v; }
void Component385::set_opacity(float v) { m_impl->m_opacity = v; }
void Component385::set_width(float v) { m_impl->m_width = v; }
void Component385::set_height(float v) { m_impl->m_height = v; }
void Component385::set_child(Component384* child) { m_impl->m_child = child; }
void Component385::on_click() {}
void Component385::on_hover() {}
void Component385::on_focus() {}
