#include "Component214.h"

#include <cstdint>

struct Component214::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component213* m_child = nullptr;
};

Component214::Component214() : m_impl(new Impl) {}
Component214::~Component214() { delete m_impl; }

float Component214::color() const { return m_impl->m_color; }
float Component214::width() const { return m_impl->m_width; }
void Component214::set_color(float v) { m_impl->m_color = v; }
void Component214::set_opacity(float v) { m_impl->m_opacity = v; }
void Component214::set_width(float v) { m_impl->m_width = v; }
void Component214::set_height(float v) { m_impl->m_height = v; }
void Component214::set_child(Component213* child) { m_impl->m_child = child; }
void Component214::on_click() {}
void Component214::on_hover() {}
void Component214::on_focus() {}
