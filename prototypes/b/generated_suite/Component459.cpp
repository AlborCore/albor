#include "Component459.h"

#include <cstdint>

struct Component459::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component458* m_child = nullptr;
};

Component459::Component459() :
    m_impl(new Impl) {}
Component459::~Component459() { delete m_impl; }

float Component459::color() const { return m_impl->m_color; }
float Component459::width() const { return m_impl->m_width; }
void Component459::set_color(float v) { m_impl->m_color = v; }
void Component459::set_opacity(float v) { m_impl->m_opacity = v; }
void Component459::set_width(float v) { m_impl->m_width = v; }
void Component459::set_height(float v) { m_impl->m_height = v; }
void Component459::set_child(Component458* child) { m_impl->m_child = child; }
void Component459::on_click() {}
void Component459::on_hover() {}
void Component459::on_focus() {}
