#include "Component364.h"

#include <cstdint>

struct Component364::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component363* m_child = nullptr;
};

Component364::Component364() :
    m_impl(new Impl) {}
Component364::~Component364() { delete m_impl; }

float Component364::color() const { return m_impl->m_color; }
float Component364::width() const { return m_impl->m_width; }
void Component364::set_color(float v) { m_impl->m_color = v; }
void Component364::set_opacity(float v) { m_impl->m_opacity = v; }
void Component364::set_width(float v) { m_impl->m_width = v; }
void Component364::set_height(float v) { m_impl->m_height = v; }
void Component364::set_child(Component363* child) { m_impl->m_child = child; }
void Component364::on_click() {}
void Component364::on_hover() {}
void Component364::on_focus() {}
