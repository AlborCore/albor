#include "Component102.h"

#include <cstdint>

struct Component102::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component101* m_child = nullptr;
};

Component102::Component102() : m_impl(new Impl) {}
Component102::~Component102() { delete m_impl; }

float Component102::color() const { return m_impl->m_color; }
float Component102::width() const { return m_impl->m_width; }
void Component102::set_color(float v) { m_impl->m_color = v; }
void Component102::set_opacity(float v) { m_impl->m_opacity = v; }
void Component102::set_width(float v) { m_impl->m_width = v; }
void Component102::set_height(float v) { m_impl->m_height = v; }
void Component102::set_child(Component101* child) { m_impl->m_child = child; }
void Component102::on_click() {}
void Component102::on_hover() {}
void Component102::on_focus() {}
