#include "Component053.h"

#include <cstdint>

struct Component053::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component052* m_child = nullptr;
};

Component053::Component053() : m_impl(new Impl) {}
Component053::~Component053() { delete m_impl; }

float Component053::color() const { return m_impl->m_color; }
float Component053::width() const { return m_impl->m_width; }
void Component053::set_color(float v) { m_impl->m_color = v; }
void Component053::set_opacity(float v) { m_impl->m_opacity = v; }
void Component053::set_width(float v) { m_impl->m_width = v; }
void Component053::set_height(float v) { m_impl->m_height = v; }
void Component053::set_child(Component052* child) { m_impl->m_child = child; }
void Component053::on_click() {}
void Component053::on_hover() {}
void Component053::on_focus() {}
