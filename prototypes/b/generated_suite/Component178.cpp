#include "Component178.h"

#include <cstdint>

struct Component178::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component177* m_child = nullptr;
};

Component178::Component178() :
    m_impl(new Impl) {}
Component178::~Component178() { delete m_impl; }

float Component178::color() const { return m_impl->m_color; }
float Component178::width() const { return m_impl->m_width; }
void Component178::set_color(float v) { m_impl->m_color = v; }
void Component178::set_opacity(float v) { m_impl->m_opacity = v; }
void Component178::set_width(float v) { m_impl->m_width = v; }
void Component178::set_height(float v) { m_impl->m_height = v; }
void Component178::set_child(Component177* child) { m_impl->m_child = child; }
void Component178::on_click() {}
void Component178::on_hover() {}
void Component178::on_focus() {}
