#include "Component490.h"

#include <cstdint>

struct Component490::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component489* m_child = nullptr;
};

Component490::Component490() :
    m_impl(new Impl) {}
Component490::~Component490() { delete m_impl; }

float Component490::color() const { return m_impl->m_color; }
float Component490::width() const { return m_impl->m_width; }
void Component490::set_color(float v) { m_impl->m_color = v; }
void Component490::set_opacity(float v) { m_impl->m_opacity = v; }
void Component490::set_width(float v) { m_impl->m_width = v; }
void Component490::set_height(float v) { m_impl->m_height = v; }
void Component490::set_child(Component489* child) { m_impl->m_child = child; }
void Component490::on_click() {}
void Component490::on_hover() {}
void Component490::on_focus() {}
