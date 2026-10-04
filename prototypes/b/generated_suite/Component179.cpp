#include "Component179.h"

#include <cstdint>

struct Component179::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component178* m_child = nullptr;
};

Component179::Component179() : m_impl(new Impl) {}
Component179::~Component179() { delete m_impl; }

float Component179::color() const { return m_impl->m_color; }
float Component179::width() const { return m_impl->m_width; }
void Component179::set_color(float v) { m_impl->m_color = v; }
void Component179::set_opacity(float v) { m_impl->m_opacity = v; }
void Component179::set_width(float v) { m_impl->m_width = v; }
void Component179::set_height(float v) { m_impl->m_height = v; }
void Component179::set_child(Component178* child) { m_impl->m_child = child; }
void Component179::on_click() {}
void Component179::on_hover() {}
void Component179::on_focus() {}
