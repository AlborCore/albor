#include "Component189.h"

#include <cstdint>

struct Component189::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component188* m_child = nullptr;
};

Component189::Component189() :
    m_impl(new Impl) {}
Component189::~Component189() { delete m_impl; }

float Component189::color() const { return m_impl->m_color; }
float Component189::width() const { return m_impl->m_width; }
void Component189::set_color(float v) { m_impl->m_color = v; }
void Component189::set_opacity(float v) { m_impl->m_opacity = v; }
void Component189::set_width(float v) { m_impl->m_width = v; }
void Component189::set_height(float v) { m_impl->m_height = v; }
void Component189::set_child(Component188* child) { m_impl->m_child = child; }
void Component189::on_click() {}
void Component189::on_hover() {}
void Component189::on_focus() {}
