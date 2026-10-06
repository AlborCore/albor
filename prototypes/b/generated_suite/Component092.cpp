#include "Component092.h"

#include <cstdint>

struct Component092::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component091* m_child = nullptr;
};

Component092::Component092() :
    m_impl(new Impl) {}
Component092::~Component092() { delete m_impl; }

float Component092::color() const { return m_impl->m_color; }
float Component092::width() const { return m_impl->m_width; }
void Component092::set_color(float v) { m_impl->m_color = v; }
void Component092::set_opacity(float v) { m_impl->m_opacity = v; }
void Component092::set_width(float v) { m_impl->m_width = v; }
void Component092::set_height(float v) { m_impl->m_height = v; }
void Component092::set_child(Component091* child) { m_impl->m_child = child; }
void Component092::on_click() {}
void Component092::on_hover() {}
void Component092::on_focus() {}
