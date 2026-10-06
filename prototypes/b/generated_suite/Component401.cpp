#include "Component401.h"

#include <cstdint>

struct Component401::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component400* m_child = nullptr;
};

Component401::Component401() :
    m_impl(new Impl) {}
Component401::~Component401() { delete m_impl; }

float Component401::color() const { return m_impl->m_color; }
float Component401::width() const { return m_impl->m_width; }
void Component401::set_color(float v) { m_impl->m_color = v; }
void Component401::set_opacity(float v) { m_impl->m_opacity = v; }
void Component401::set_width(float v) { m_impl->m_width = v; }
void Component401::set_height(float v) { m_impl->m_height = v; }
void Component401::set_child(Component400* child) { m_impl->m_child = child; }
void Component401::on_click() {}
void Component401::on_hover() {}
void Component401::on_focus() {}
