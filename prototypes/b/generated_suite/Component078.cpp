#include "Component078.h"

#include <cstdint>

struct Component078::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component077* m_child = nullptr;
};

Component078::Component078() : m_impl(new Impl) {}
Component078::~Component078() { delete m_impl; }

float Component078::color() const { return m_impl->m_color; }
float Component078::width() const { return m_impl->m_width; }
void Component078::set_color(float v) { m_impl->m_color = v; }
void Component078::set_opacity(float v) { m_impl->m_opacity = v; }
void Component078::set_width(float v) { m_impl->m_width = v; }
void Component078::set_height(float v) { m_impl->m_height = v; }
void Component078::set_child(Component077* child) { m_impl->m_child = child; }
void Component078::on_click() {}
void Component078::on_hover() {}
void Component078::on_focus() {}
