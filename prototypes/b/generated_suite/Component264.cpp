#include "Component264.h"

#include <cstdint>

struct Component264::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component263* m_child = nullptr;
};

Component264::Component264() : m_impl(new Impl) {}
Component264::~Component264() { delete m_impl; }

float Component264::color() const { return m_impl->m_color; }
float Component264::width() const { return m_impl->m_width; }
void Component264::set_color(float v) { m_impl->m_color = v; }
void Component264::set_opacity(float v) { m_impl->m_opacity = v; }
void Component264::set_width(float v) { m_impl->m_width = v; }
void Component264::set_height(float v) { m_impl->m_height = v; }
void Component264::set_child(Component263* child) { m_impl->m_child = child; }
void Component264::on_click() {}
void Component264::on_hover() {}
void Component264::on_focus() {}
