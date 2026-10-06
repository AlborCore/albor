#include "Component461.h"

#include <cstdint>

struct Component461::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component460* m_child = nullptr;
};

Component461::Component461() :
    m_impl(new Impl) {}
Component461::~Component461() { delete m_impl; }

float Component461::color() const { return m_impl->m_color; }
float Component461::width() const { return m_impl->m_width; }
void Component461::set_color(float v) { m_impl->m_color = v; }
void Component461::set_opacity(float v) { m_impl->m_opacity = v; }
void Component461::set_width(float v) { m_impl->m_width = v; }
void Component461::set_height(float v) { m_impl->m_height = v; }
void Component461::set_child(Component460* child) { m_impl->m_child = child; }
void Component461::on_click() {}
void Component461::on_hover() {}
void Component461::on_focus() {}
