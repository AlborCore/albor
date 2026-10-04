#include "Component335.h"

#include <cstdint>

struct Component335::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component334* m_child = nullptr;
};

Component335::Component335() : m_impl(new Impl) {}
Component335::~Component335() { delete m_impl; }

float Component335::color() const { return m_impl->m_color; }
float Component335::width() const { return m_impl->m_width; }
void Component335::set_color(float v) { m_impl->m_color = v; }
void Component335::set_opacity(float v) { m_impl->m_opacity = v; }
void Component335::set_width(float v) { m_impl->m_width = v; }
void Component335::set_height(float v) { m_impl->m_height = v; }
void Component335::set_child(Component334* child) { m_impl->m_child = child; }
void Component335::on_click() {}
void Component335::on_hover() {}
void Component335::on_focus() {}
