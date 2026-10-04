#include "Component091.h"

#include <cstdint>

struct Component091::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component090* m_child = nullptr;
};

Component091::Component091() :
    m_impl(new Impl) {}
Component091::~Component091() { delete m_impl; }

float Component091::color() const { return m_impl->m_color; }
float Component091::width() const { return m_impl->m_width; }
void Component091::set_color(float v) { m_impl->m_color = v; }
void Component091::set_opacity(float v) { m_impl->m_opacity = v; }
void Component091::set_width(float v) { m_impl->m_width = v; }
void Component091::set_height(float v) { m_impl->m_height = v; }
void Component091::set_child(Component090* child) { m_impl->m_child = child; }
void Component091::on_click() {}
void Component091::on_hover() {}
void Component091::on_focus() {}
