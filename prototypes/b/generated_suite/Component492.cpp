#include "Component492.h"

#include <cstdint>

struct Component492::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component491* m_child = nullptr;
};

Component492::Component492() :
    m_impl(new Impl) {}
Component492::~Component492() { delete m_impl; }

float Component492::color() const { return m_impl->m_color; }
float Component492::width() const { return m_impl->m_width; }
void Component492::set_color(float v) { m_impl->m_color = v; }
void Component492::set_opacity(float v) { m_impl->m_opacity = v; }
void Component492::set_width(float v) { m_impl->m_width = v; }
void Component492::set_height(float v) { m_impl->m_height = v; }
void Component492::set_child(Component491* child) { m_impl->m_child = child; }
void Component492::on_click() {}
void Component492::on_hover() {}
void Component492::on_focus() {}
