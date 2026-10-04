#include "Component081.h"

#include <cstdint>

struct Component081::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component080* m_child = nullptr;
};

Component081::Component081() : m_impl(new Impl) {}
Component081::~Component081() { delete m_impl; }

float Component081::color() const { return m_impl->m_color; }
float Component081::width() const { return m_impl->m_width; }
void Component081::set_color(float v) { m_impl->m_color = v; }
void Component081::set_opacity(float v) { m_impl->m_opacity = v; }
void Component081::set_width(float v) { m_impl->m_width = v; }
void Component081::set_height(float v) { m_impl->m_height = v; }
void Component081::set_child(Component080* child) { m_impl->m_child = child; }
void Component081::on_click() {}
void Component081::on_hover() {}
void Component081::on_focus() {}
