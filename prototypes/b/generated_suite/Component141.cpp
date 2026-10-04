#include "Component141.h"

#include <cstdint>

struct Component141::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component140* m_child = nullptr;
};

Component141::Component141() : m_impl(new Impl) {}
Component141::~Component141() { delete m_impl; }

float Component141::color() const { return m_impl->m_color; }
float Component141::width() const { return m_impl->m_width; }
void Component141::set_color(float v) { m_impl->m_color = v; }
void Component141::set_opacity(float v) { m_impl->m_opacity = v; }
void Component141::set_width(float v) { m_impl->m_width = v; }
void Component141::set_height(float v) { m_impl->m_height = v; }
void Component141::set_child(Component140* child) { m_impl->m_child = child; }
void Component141::on_click() {}
void Component141::on_hover() {}
void Component141::on_focus() {}
