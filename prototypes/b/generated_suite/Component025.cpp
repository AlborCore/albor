#include "Component025.h"

#include <cstdint>

struct Component025::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component024* m_child = nullptr;
};

Component025::Component025() : m_impl(new Impl) {}
Component025::~Component025() { delete m_impl; }

float Component025::color() const { return m_impl->m_color; }
float Component025::width() const { return m_impl->m_width; }
void Component025::set_color(float v) { m_impl->m_color = v; }
void Component025::set_opacity(float v) { m_impl->m_opacity = v; }
void Component025::set_width(float v) { m_impl->m_width = v; }
void Component025::set_height(float v) { m_impl->m_height = v; }
void Component025::set_child(Component024* child) { m_impl->m_child = child; }
void Component025::on_click() {}
void Component025::on_hover() {}
void Component025::on_focus() {}
