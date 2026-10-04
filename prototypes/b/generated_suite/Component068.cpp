#include "Component068.h"

#include <cstdint>

struct Component068::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component067* m_child = nullptr;
};

Component068::Component068() : m_impl(new Impl) {}
Component068::~Component068() { delete m_impl; }

float Component068::color() const { return m_impl->m_color; }
float Component068::width() const { return m_impl->m_width; }
void Component068::set_color(float v) { m_impl->m_color = v; }
void Component068::set_opacity(float v) { m_impl->m_opacity = v; }
void Component068::set_width(float v) { m_impl->m_width = v; }
void Component068::set_height(float v) { m_impl->m_height = v; }
void Component068::set_child(Component067* child) { m_impl->m_child = child; }
void Component068::on_click() {}
void Component068::on_hover() {}
void Component068::on_focus() {}
