#include "Component013.h"

#include <cstdint>

struct Component013::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component012* m_child = nullptr;
};

Component013::Component013() :
    m_impl(new Impl) {}
Component013::~Component013() { delete m_impl; }

float Component013::color() const { return m_impl->m_color; }
float Component013::width() const { return m_impl->m_width; }
void Component013::set_color(float v) { m_impl->m_color = v; }
void Component013::set_opacity(float v) { m_impl->m_opacity = v; }
void Component013::set_width(float v) { m_impl->m_width = v; }
void Component013::set_height(float v) { m_impl->m_height = v; }
void Component013::set_child(Component012* child) { m_impl->m_child = child; }
void Component013::on_click() {}
void Component013::on_hover() {}
void Component013::on_focus() {}
