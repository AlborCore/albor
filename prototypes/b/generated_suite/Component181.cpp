#include "Component181.h"

#include <cstdint>

struct Component181::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component180* m_child = nullptr;
};

Component181::Component181() :
    m_impl(new Impl) {}
Component181::~Component181() { delete m_impl; }

float Component181::color() const { return m_impl->m_color; }
float Component181::width() const { return m_impl->m_width; }
void Component181::set_color(float v) { m_impl->m_color = v; }
void Component181::set_opacity(float v) { m_impl->m_opacity = v; }
void Component181::set_width(float v) { m_impl->m_width = v; }
void Component181::set_height(float v) { m_impl->m_height = v; }
void Component181::set_child(Component180* child) { m_impl->m_child = child; }
void Component181::on_click() {}
void Component181::on_hover() {}
void Component181::on_focus() {}
