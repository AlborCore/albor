#include "Component058.h"

#include <cstdint>

struct Component058::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component057* m_child = nullptr;
};

Component058::Component058() : m_impl(new Impl) {}
Component058::~Component058() { delete m_impl; }

float Component058::color() const { return m_impl->m_color; }
float Component058::width() const { return m_impl->m_width; }
void Component058::set_color(float v) { m_impl->m_color = v; }
void Component058::set_opacity(float v) { m_impl->m_opacity = v; }
void Component058::set_width(float v) { m_impl->m_width = v; }
void Component058::set_height(float v) { m_impl->m_height = v; }
void Component058::set_child(Component057* child) { m_impl->m_child = child; }
void Component058::on_click() {}
void Component058::on_hover() {}
void Component058::on_focus() {}
