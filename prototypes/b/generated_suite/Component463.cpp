#include "Component463.h"

#include <cstdint>

struct Component463::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component462* m_child = nullptr;
};

Component463::Component463() : m_impl(new Impl) {}
Component463::~Component463() { delete m_impl; }

float Component463::color() const { return m_impl->m_color; }
float Component463::width() const { return m_impl->m_width; }
void Component463::set_color(float v) { m_impl->m_color = v; }
void Component463::set_opacity(float v) { m_impl->m_opacity = v; }
void Component463::set_width(float v) { m_impl->m_width = v; }
void Component463::set_height(float v) { m_impl->m_height = v; }
void Component463::set_child(Component462* child) { m_impl->m_child = child; }
void Component463::on_click() {}
void Component463::on_hover() {}
void Component463::on_focus() {}
