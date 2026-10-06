#include "Component394.h"

#include <cstdint>

struct Component394::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component393* m_child = nullptr;
};

Component394::Component394() :
    m_impl(new Impl) {}
Component394::~Component394() { delete m_impl; }

float Component394::color() const { return m_impl->m_color; }
float Component394::width() const { return m_impl->m_width; }
void Component394::set_color(float v) { m_impl->m_color = v; }
void Component394::set_opacity(float v) { m_impl->m_opacity = v; }
void Component394::set_width(float v) { m_impl->m_width = v; }
void Component394::set_height(float v) { m_impl->m_height = v; }
void Component394::set_child(Component393* child) { m_impl->m_child = child; }
void Component394::on_click() {}
void Component394::on_hover() {}
void Component394::on_focus() {}
