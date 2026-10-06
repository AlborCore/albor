#include "Component419.h"

#include <cstdint>

struct Component419::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component418* m_child = nullptr;
};

Component419::Component419() :
    m_impl(new Impl) {}
Component419::~Component419() { delete m_impl; }

float Component419::color() const { return m_impl->m_color; }
float Component419::width() const { return m_impl->m_width; }
void Component419::set_color(float v) { m_impl->m_color = v; }
void Component419::set_opacity(float v) { m_impl->m_opacity = v; }
void Component419::set_width(float v) { m_impl->m_width = v; }
void Component419::set_height(float v) { m_impl->m_height = v; }
void Component419::set_child(Component418* child) { m_impl->m_child = child; }
void Component419::on_click() {}
void Component419::on_hover() {}
void Component419::on_focus() {}
