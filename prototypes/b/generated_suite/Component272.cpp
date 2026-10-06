#include "Component272.h"

#include <cstdint>

struct Component272::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component271* m_child = nullptr;
};

Component272::Component272() :
    m_impl(new Impl) {}
Component272::~Component272() { delete m_impl; }

float Component272::color() const { return m_impl->m_color; }
float Component272::width() const { return m_impl->m_width; }
void Component272::set_color(float v) { m_impl->m_color = v; }
void Component272::set_opacity(float v) { m_impl->m_opacity = v; }
void Component272::set_width(float v) { m_impl->m_width = v; }
void Component272::set_height(float v) { m_impl->m_height = v; }
void Component272::set_child(Component271* child) { m_impl->m_child = child; }
void Component272::on_click() {}
void Component272::on_hover() {}
void Component272::on_focus() {}
