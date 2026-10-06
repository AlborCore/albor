#include "Component398.h"

#include <cstdint>

struct Component398::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component397* m_child = nullptr;
};

Component398::Component398() :
    m_impl(new Impl) {}
Component398::~Component398() { delete m_impl; }

float Component398::color() const { return m_impl->m_color; }
float Component398::width() const { return m_impl->m_width; }
void Component398::set_color(float v) { m_impl->m_color = v; }
void Component398::set_opacity(float v) { m_impl->m_opacity = v; }
void Component398::set_width(float v) { m_impl->m_width = v; }
void Component398::set_height(float v) { m_impl->m_height = v; }
void Component398::set_child(Component397* child) { m_impl->m_child = child; }
void Component398::on_click() {}
void Component398::on_hover() {}
void Component398::on_focus() {}
