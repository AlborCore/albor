#include "Component199.h"

#include <cstdint>

struct Component199::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component198* m_child = nullptr;
};

Component199::Component199() :
    m_impl(new Impl) {}
Component199::~Component199() { delete m_impl; }

float Component199::color() const { return m_impl->m_color; }
float Component199::width() const { return m_impl->m_width; }
void Component199::set_color(float v) { m_impl->m_color = v; }
void Component199::set_opacity(float v) { m_impl->m_opacity = v; }
void Component199::set_width(float v) { m_impl->m_width = v; }
void Component199::set_height(float v) { m_impl->m_height = v; }
void Component199::set_child(Component198* child) { m_impl->m_child = child; }
void Component199::on_click() {}
void Component199::on_hover() {}
void Component199::on_focus() {}
