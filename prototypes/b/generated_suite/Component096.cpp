#include "Component096.h"

#include <cstdint>

struct Component096::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component095* m_child = nullptr;
};

Component096::Component096() :
    m_impl(new Impl) {}
Component096::~Component096() { delete m_impl; }

float Component096::color() const { return m_impl->m_color; }
float Component096::width() const { return m_impl->m_width; }
void Component096::set_color(float v) { m_impl->m_color = v; }
void Component096::set_opacity(float v) { m_impl->m_opacity = v; }
void Component096::set_width(float v) { m_impl->m_width = v; }
void Component096::set_height(float v) { m_impl->m_height = v; }
void Component096::set_child(Component095* child) { m_impl->m_child = child; }
void Component096::on_click() {}
void Component096::on_hover() {}
void Component096::on_focus() {}
