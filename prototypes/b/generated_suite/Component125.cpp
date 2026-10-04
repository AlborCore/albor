#include "Component125.h"

#include <cstdint>

struct Component125::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component124* m_child = nullptr;
};

Component125::Component125() : m_impl(new Impl) {}
Component125::~Component125() { delete m_impl; }

float Component125::color() const { return m_impl->m_color; }
float Component125::width() const { return m_impl->m_width; }
void Component125::set_color(float v) { m_impl->m_color = v; }
void Component125::set_opacity(float v) { m_impl->m_opacity = v; }
void Component125::set_width(float v) { m_impl->m_width = v; }
void Component125::set_height(float v) { m_impl->m_height = v; }
void Component125::set_child(Component124* child) { m_impl->m_child = child; }
void Component125::on_click() {}
void Component125::on_hover() {}
void Component125::on_focus() {}
