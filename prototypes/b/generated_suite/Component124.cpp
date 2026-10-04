#include "Component124.h"

#include <cstdint>

struct Component124::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component123* m_child = nullptr;
};

Component124::Component124() : m_impl(new Impl) {}
Component124::~Component124() { delete m_impl; }

float Component124::color() const { return m_impl->m_color; }
float Component124::width() const { return m_impl->m_width; }
void Component124::set_color(float v) { m_impl->m_color = v; }
void Component124::set_opacity(float v) { m_impl->m_opacity = v; }
void Component124::set_width(float v) { m_impl->m_width = v; }
void Component124::set_height(float v) { m_impl->m_height = v; }
void Component124::set_child(Component123* child) { m_impl->m_child = child; }
void Component124::on_click() {}
void Component124::on_hover() {}
void Component124::on_focus() {}
