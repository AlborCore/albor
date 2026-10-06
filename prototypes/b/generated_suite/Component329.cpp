#include "Component329.h"

#include <cstdint>

struct Component329::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component328* m_child = nullptr;
};

Component329::Component329() :
    m_impl(new Impl) {}
Component329::~Component329() { delete m_impl; }

float Component329::color() const { return m_impl->m_color; }
float Component329::width() const { return m_impl->m_width; }
void Component329::set_color(float v) { m_impl->m_color = v; }
void Component329::set_opacity(float v) { m_impl->m_opacity = v; }
void Component329::set_width(float v) { m_impl->m_width = v; }
void Component329::set_height(float v) { m_impl->m_height = v; }
void Component329::set_child(Component328* child) { m_impl->m_child = child; }
void Component329::on_click() {}
void Component329::on_hover() {}
void Component329::on_focus() {}
