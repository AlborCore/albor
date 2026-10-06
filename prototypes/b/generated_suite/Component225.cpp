#include "Component225.h"

#include <cstdint>

struct Component225::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component224* m_child = nullptr;
};

Component225::Component225() :
    m_impl(new Impl) {}
Component225::~Component225() { delete m_impl; }

float Component225::color() const { return m_impl->m_color; }
float Component225::width() const { return m_impl->m_width; }
void Component225::set_color(float v) { m_impl->m_color = v; }
void Component225::set_opacity(float v) { m_impl->m_opacity = v; }
void Component225::set_width(float v) { m_impl->m_width = v; }
void Component225::set_height(float v) { m_impl->m_height = v; }
void Component225::set_child(Component224* child) { m_impl->m_child = child; }
void Component225::on_click() {}
void Component225::on_hover() {}
void Component225::on_focus() {}
