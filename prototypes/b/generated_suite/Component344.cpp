#include "Component344.h"

#include <cstdint>

struct Component344::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component343* m_child = nullptr;
};

Component344::Component344() : m_impl(new Impl) {}
Component344::~Component344() { delete m_impl; }

float Component344::color() const { return m_impl->m_color; }
float Component344::width() const { return m_impl->m_width; }
void Component344::set_color(float v) { m_impl->m_color = v; }
void Component344::set_opacity(float v) { m_impl->m_opacity = v; }
void Component344::set_width(float v) { m_impl->m_width = v; }
void Component344::set_height(float v) { m_impl->m_height = v; }
void Component344::set_child(Component343* child) { m_impl->m_child = child; }
void Component344::on_click() {}
void Component344::on_hover() {}
void Component344::on_focus() {}
