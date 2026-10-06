#include "Component316.h"

#include <cstdint>

struct Component316::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component315* m_child = nullptr;
};

Component316::Component316() :
    m_impl(new Impl) {}
Component316::~Component316() { delete m_impl; }

float Component316::color() const { return m_impl->m_color; }
float Component316::width() const { return m_impl->m_width; }
void Component316::set_color(float v) { m_impl->m_color = v; }
void Component316::set_opacity(float v) { m_impl->m_opacity = v; }
void Component316::set_width(float v) { m_impl->m_width = v; }
void Component316::set_height(float v) { m_impl->m_height = v; }
void Component316::set_child(Component315* child) { m_impl->m_child = child; }
void Component316::on_click() {}
void Component316::on_hover() {}
void Component316::on_focus() {}
