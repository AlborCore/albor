#include "Component299.h"

#include <cstdint>

struct Component299::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component298* m_child = nullptr;
};

Component299::Component299() : m_impl(new Impl) {}
Component299::~Component299() { delete m_impl; }

float Component299::color() const { return m_impl->m_color; }
float Component299::width() const { return m_impl->m_width; }
void Component299::set_color(float v) { m_impl->m_color = v; }
void Component299::set_opacity(float v) { m_impl->m_opacity = v; }
void Component299::set_width(float v) { m_impl->m_width = v; }
void Component299::set_height(float v) { m_impl->m_height = v; }
void Component299::set_child(Component298* child) { m_impl->m_child = child; }
void Component299::on_click() {}
void Component299::on_hover() {}
void Component299::on_focus() {}
