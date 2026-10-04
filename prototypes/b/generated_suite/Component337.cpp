#include "Component337.h"

#include <cstdint>

struct Component337::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component336* m_child = nullptr;
};

Component337::Component337() : m_impl(new Impl) {}
Component337::~Component337() { delete m_impl; }

float Component337::color() const { return m_impl->m_color; }
float Component337::width() const { return m_impl->m_width; }
void Component337::set_color(float v) { m_impl->m_color = v; }
void Component337::set_opacity(float v) { m_impl->m_opacity = v; }
void Component337::set_width(float v) { m_impl->m_width = v; }
void Component337::set_height(float v) { m_impl->m_height = v; }
void Component337::set_child(Component336* child) { m_impl->m_child = child; }
void Component337::on_click() {}
void Component337::on_hover() {}
void Component337::on_focus() {}
