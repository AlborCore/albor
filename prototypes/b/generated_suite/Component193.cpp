#include "Component193.h"

#include <cstdint>

struct Component193::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component192* m_child = nullptr;
};

Component193::Component193() : m_impl(new Impl) {}
Component193::~Component193() { delete m_impl; }

float Component193::color() const { return m_impl->m_color; }
float Component193::width() const { return m_impl->m_width; }
void Component193::set_color(float v) { m_impl->m_color = v; }
void Component193::set_opacity(float v) { m_impl->m_opacity = v; }
void Component193::set_width(float v) { m_impl->m_width = v; }
void Component193::set_height(float v) { m_impl->m_height = v; }
void Component193::set_child(Component192* child) { m_impl->m_child = child; }
void Component193::on_click() {}
void Component193::on_hover() {}
void Component193::on_focus() {}
