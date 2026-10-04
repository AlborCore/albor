#include "Component204.h"

#include <cstdint>

struct Component204::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component203* m_child = nullptr;
};

Component204::Component204() : m_impl(new Impl) {}
Component204::~Component204() { delete m_impl; }

float Component204::color() const { return m_impl->m_color; }
float Component204::width() const { return m_impl->m_width; }
void Component204::set_color(float v) { m_impl->m_color = v; }
void Component204::set_opacity(float v) { m_impl->m_opacity = v; }
void Component204::set_width(float v) { m_impl->m_width = v; }
void Component204::set_height(float v) { m_impl->m_height = v; }
void Component204::set_child(Component203* child) { m_impl->m_child = child; }
void Component204::on_click() {}
void Component204::on_hover() {}
void Component204::on_focus() {}
