#include "Component166.h"

#include <cstdint>

struct Component166::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component165* m_child = nullptr;
};

Component166::Component166() :
    m_impl(new Impl) {}
Component166::~Component166() { delete m_impl; }

float Component166::color() const { return m_impl->m_color; }
float Component166::width() const { return m_impl->m_width; }
void Component166::set_color(float v) { m_impl->m_color = v; }
void Component166::set_opacity(float v) { m_impl->m_opacity = v; }
void Component166::set_width(float v) { m_impl->m_width = v; }
void Component166::set_height(float v) { m_impl->m_height = v; }
void Component166::set_child(Component165* child) { m_impl->m_child = child; }
void Component166::on_click() {}
void Component166::on_hover() {}
void Component166::on_focus() {}
