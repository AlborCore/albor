#include "Component183.h"

#include <cstdint>

struct Component183::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component182* m_child = nullptr;
};

Component183::Component183() :
    m_impl(new Impl) {}
Component183::~Component183() { delete m_impl; }

float Component183::color() const { return m_impl->m_color; }
float Component183::width() const { return m_impl->m_width; }
void Component183::set_color(float v) { m_impl->m_color = v; }
void Component183::set_opacity(float v) { m_impl->m_opacity = v; }
void Component183::set_width(float v) { m_impl->m_width = v; }
void Component183::set_height(float v) { m_impl->m_height = v; }
void Component183::set_child(Component182* child) { m_impl->m_child = child; }
void Component183::on_click() {}
void Component183::on_hover() {}
void Component183::on_focus() {}
