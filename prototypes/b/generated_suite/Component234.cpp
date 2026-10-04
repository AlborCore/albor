#include "Component234.h"

#include <cstdint>

struct Component234::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component233* m_child = nullptr;
};

Component234::Component234() :
    m_impl(new Impl) {}
Component234::~Component234() { delete m_impl; }

float Component234::color() const { return m_impl->m_color; }
float Component234::width() const { return m_impl->m_width; }
void Component234::set_color(float v) { m_impl->m_color = v; }
void Component234::set_opacity(float v) { m_impl->m_opacity = v; }
void Component234::set_width(float v) { m_impl->m_width = v; }
void Component234::set_height(float v) { m_impl->m_height = v; }
void Component234::set_child(Component233* child) { m_impl->m_child = child; }
void Component234::on_click() {}
void Component234::on_hover() {}
void Component234::on_focus() {}
