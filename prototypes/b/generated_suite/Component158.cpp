#include "Component158.h"

#include <cstdint>

struct Component158::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component157* m_child = nullptr;
};

Component158::Component158() : m_impl(new Impl) {}
Component158::~Component158() { delete m_impl; }

float Component158::color() const { return m_impl->m_color; }
float Component158::width() const { return m_impl->m_width; }
void Component158::set_color(float v) { m_impl->m_color = v; }
void Component158::set_opacity(float v) { m_impl->m_opacity = v; }
void Component158::set_width(float v) { m_impl->m_width = v; }
void Component158::set_height(float v) { m_impl->m_height = v; }
void Component158::set_child(Component157* child) { m_impl->m_child = child; }
void Component158::on_click() {}
void Component158::on_hover() {}
void Component158::on_focus() {}
