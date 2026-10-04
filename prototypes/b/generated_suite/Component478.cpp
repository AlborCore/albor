#include "Component478.h"

#include <cstdint>

struct Component478::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component477* m_child = nullptr;
};

Component478::Component478() : m_impl(new Impl) {}
Component478::~Component478() { delete m_impl; }

float Component478::color() const { return m_impl->m_color; }
float Component478::width() const { return m_impl->m_width; }
void Component478::set_color(float v) { m_impl->m_color = v; }
void Component478::set_opacity(float v) { m_impl->m_opacity = v; }
void Component478::set_width(float v) { m_impl->m_width = v; }
void Component478::set_height(float v) { m_impl->m_height = v; }
void Component478::set_child(Component477* child) { m_impl->m_child = child; }
void Component478::on_click() {}
void Component478::on_hover() {}
void Component478::on_focus() {}
