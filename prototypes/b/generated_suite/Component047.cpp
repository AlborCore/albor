#include "Component047.h"

#include <cstdint>

struct Component047::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component046* m_child = nullptr;
};

Component047::Component047() :
    m_impl(new Impl) {}
Component047::~Component047() { delete m_impl; }

float Component047::color() const { return m_impl->m_color; }
float Component047::width() const { return m_impl->m_width; }
void Component047::set_color(float v) { m_impl->m_color = v; }
void Component047::set_opacity(float v) { m_impl->m_opacity = v; }
void Component047::set_width(float v) { m_impl->m_width = v; }
void Component047::set_height(float v) { m_impl->m_height = v; }
void Component047::set_child(Component046* child) { m_impl->m_child = child; }
void Component047::on_click() {}
void Component047::on_hover() {}
void Component047::on_focus() {}
