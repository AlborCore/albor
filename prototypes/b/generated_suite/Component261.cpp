#include "Component261.h"

#include <cstdint>

struct Component261::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component260* m_child = nullptr;
};

Component261::Component261() : m_impl(new Impl) {}
Component261::~Component261() { delete m_impl; }

float Component261::color() const { return m_impl->m_color; }
float Component261::width() const { return m_impl->m_width; }
void Component261::set_color(float v) { m_impl->m_color = v; }
void Component261::set_opacity(float v) { m_impl->m_opacity = v; }
void Component261::set_width(float v) { m_impl->m_width = v; }
void Component261::set_height(float v) { m_impl->m_height = v; }
void Component261::set_child(Component260* child) { m_impl->m_child = child; }
void Component261::on_click() {}
void Component261::on_hover() {}
void Component261::on_focus() {}
