#include "Component114.h"

#include <cstdint>

struct Component114::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component113* m_child = nullptr;
};

Component114::Component114() : m_impl(new Impl) {}
Component114::~Component114() { delete m_impl; }

float Component114::color() const { return m_impl->m_color; }
float Component114::width() const { return m_impl->m_width; }
void Component114::set_color(float v) { m_impl->m_color = v; }
void Component114::set_opacity(float v) { m_impl->m_opacity = v; }
void Component114::set_width(float v) { m_impl->m_width = v; }
void Component114::set_height(float v) { m_impl->m_height = v; }
void Component114::set_child(Component113* child) { m_impl->m_child = child; }
void Component114::on_click() {}
void Component114::on_hover() {}
void Component114::on_focus() {}
