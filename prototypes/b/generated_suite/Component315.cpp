#include "Component315.h"

#include <cstdint>

struct Component315::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component314* m_child = nullptr;
};

Component315::Component315() :
    m_impl(new Impl) {}
Component315::~Component315() { delete m_impl; }

float Component315::color() const { return m_impl->m_color; }
float Component315::width() const { return m_impl->m_width; }
void Component315::set_color(float v) { m_impl->m_color = v; }
void Component315::set_opacity(float v) { m_impl->m_opacity = v; }
void Component315::set_width(float v) { m_impl->m_width = v; }
void Component315::set_height(float v) { m_impl->m_height = v; }
void Component315::set_child(Component314* child) { m_impl->m_child = child; }
void Component315::on_click() {}
void Component315::on_hover() {}
void Component315::on_focus() {}
