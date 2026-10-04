#include "Component205.h"

#include <cstdint>

struct Component205::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component204* m_child = nullptr;
};

Component205::Component205() : m_impl(new Impl) {}
Component205::~Component205() { delete m_impl; }

float Component205::color() const { return m_impl->m_color; }
float Component205::width() const { return m_impl->m_width; }
void Component205::set_color(float v) { m_impl->m_color = v; }
void Component205::set_opacity(float v) { m_impl->m_opacity = v; }
void Component205::set_width(float v) { m_impl->m_width = v; }
void Component205::set_height(float v) { m_impl->m_height = v; }
void Component205::set_child(Component204* child) { m_impl->m_child = child; }
void Component205::on_click() {}
void Component205::on_hover() {}
void Component205::on_focus() {}
