#include "Component085.h"

#include <cstdint>

struct Component085::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component084* m_child = nullptr;
};

Component085::Component085() :
    m_impl(new Impl) {}
Component085::~Component085() { delete m_impl; }

float Component085::color() const { return m_impl->m_color; }
float Component085::width() const { return m_impl->m_width; }
void Component085::set_color(float v) { m_impl->m_color = v; }
void Component085::set_opacity(float v) { m_impl->m_opacity = v; }
void Component085::set_width(float v) { m_impl->m_width = v; }
void Component085::set_height(float v) { m_impl->m_height = v; }
void Component085::set_child(Component084* child) { m_impl->m_child = child; }
void Component085::on_click() {}
void Component085::on_hover() {}
void Component085::on_focus() {}
