#include "Component300.h"

#include <cstdint>

struct Component300::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component299* m_child = nullptr;
};

Component300::Component300() : m_impl(new Impl) {}
Component300::~Component300() { delete m_impl; }

float Component300::color() const { return m_impl->m_color; }
float Component300::width() const { return m_impl->m_width; }
void Component300::set_color(float v) { m_impl->m_color = v; }
void Component300::set_opacity(float v) { m_impl->m_opacity = v; }
void Component300::set_width(float v) { m_impl->m_width = v; }
void Component300::set_height(float v) { m_impl->m_height = v; }
void Component300::set_child(Component299* child) { m_impl->m_child = child; }
void Component300::on_click() {}
void Component300::on_hover() {}
void Component300::on_focus() {}
