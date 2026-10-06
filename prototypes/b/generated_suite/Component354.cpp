#include "Component354.h"

#include <cstdint>

struct Component354::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component353* m_child = nullptr;
};

Component354::Component354() :
    m_impl(new Impl) {}
Component354::~Component354() { delete m_impl; }

float Component354::color() const { return m_impl->m_color; }
float Component354::width() const { return m_impl->m_width; }
void Component354::set_color(float v) { m_impl->m_color = v; }
void Component354::set_opacity(float v) { m_impl->m_opacity = v; }
void Component354::set_width(float v) { m_impl->m_width = v; }
void Component354::set_height(float v) { m_impl->m_height = v; }
void Component354::set_child(Component353* child) { m_impl->m_child = child; }
void Component354::on_click() {}
void Component354::on_hover() {}
void Component354::on_focus() {}
