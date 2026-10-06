#include "Component219.h"

#include <cstdint>

struct Component219::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component218* m_child = nullptr;
};

Component219::Component219() :
    m_impl(new Impl) {}
Component219::~Component219() { delete m_impl; }

float Component219::color() const { return m_impl->m_color; }
float Component219::width() const { return m_impl->m_width; }
void Component219::set_color(float v) { m_impl->m_color = v; }
void Component219::set_opacity(float v) { m_impl->m_opacity = v; }
void Component219::set_width(float v) { m_impl->m_width = v; }
void Component219::set_height(float v) { m_impl->m_height = v; }
void Component219::set_child(Component218* child) { m_impl->m_child = child; }
void Component219::on_click() {}
void Component219::on_hover() {}
void Component219::on_focus() {}
