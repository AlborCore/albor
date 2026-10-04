#include "Component460.h"

#include <cstdint>

struct Component460::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component459* m_child = nullptr;
};

Component460::Component460() : m_impl(new Impl) {}
Component460::~Component460() { delete m_impl; }

float Component460::color() const { return m_impl->m_color; }
float Component460::width() const { return m_impl->m_width; }
void Component460::set_color(float v) { m_impl->m_color = v; }
void Component460::set_opacity(float v) { m_impl->m_opacity = v; }
void Component460::set_width(float v) { m_impl->m_width = v; }
void Component460::set_height(float v) { m_impl->m_height = v; }
void Component460::set_child(Component459* child) { m_impl->m_child = child; }
void Component460::on_click() {}
void Component460::on_hover() {}
void Component460::on_focus() {}
