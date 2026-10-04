#include "Component383.h"

#include <cstdint>

struct Component383::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component382* m_child = nullptr;
};

Component383::Component383() : m_impl(new Impl) {}
Component383::~Component383() { delete m_impl; }

float Component383::color() const { return m_impl->m_color; }
float Component383::width() const { return m_impl->m_width; }
void Component383::set_color(float v) { m_impl->m_color = v; }
void Component383::set_opacity(float v) { m_impl->m_opacity = v; }
void Component383::set_width(float v) { m_impl->m_width = v; }
void Component383::set_height(float v) { m_impl->m_height = v; }
void Component383::set_child(Component382* child) { m_impl->m_child = child; }
void Component383::on_click() {}
void Component383::on_hover() {}
void Component383::on_focus() {}
