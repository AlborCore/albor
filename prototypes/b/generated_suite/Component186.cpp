#include "Component186.h"

#include <cstdint>

struct Component186::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component185* m_child = nullptr;
};

Component186::Component186() : m_impl(new Impl) {}
Component186::~Component186() { delete m_impl; }

float Component186::color() const { return m_impl->m_color; }
float Component186::width() const { return m_impl->m_width; }
void Component186::set_color(float v) { m_impl->m_color = v; }
void Component186::set_opacity(float v) { m_impl->m_opacity = v; }
void Component186::set_width(float v) { m_impl->m_width = v; }
void Component186::set_height(float v) { m_impl->m_height = v; }
void Component186::set_child(Component185* child) { m_impl->m_child = child; }
void Component186::on_click() {}
void Component186::on_hover() {}
void Component186::on_focus() {}
