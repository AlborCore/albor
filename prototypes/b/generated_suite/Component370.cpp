#include "Component370.h"

#include <cstdint>

struct Component370::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component369* m_child = nullptr;
};

Component370::Component370() : m_impl(new Impl) {}
Component370::~Component370() { delete m_impl; }

float Component370::color() const { return m_impl->m_color; }
float Component370::width() const { return m_impl->m_width; }
void Component370::set_color(float v) { m_impl->m_color = v; }
void Component370::set_opacity(float v) { m_impl->m_opacity = v; }
void Component370::set_width(float v) { m_impl->m_width = v; }
void Component370::set_height(float v) { m_impl->m_height = v; }
void Component370::set_child(Component369* child) { m_impl->m_child = child; }
void Component370::on_click() {}
void Component370::on_hover() {}
void Component370::on_focus() {}
