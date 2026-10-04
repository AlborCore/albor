#include "Component342.h"

#include <cstdint>

struct Component342::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component341* m_child = nullptr;
};

Component342::Component342() : m_impl(new Impl) {}
Component342::~Component342() { delete m_impl; }

float Component342::color() const { return m_impl->m_color; }
float Component342::width() const { return m_impl->m_width; }
void Component342::set_color(float v) { m_impl->m_color = v; }
void Component342::set_opacity(float v) { m_impl->m_opacity = v; }
void Component342::set_width(float v) { m_impl->m_width = v; }
void Component342::set_height(float v) { m_impl->m_height = v; }
void Component342::set_child(Component341* child) { m_impl->m_child = child; }
void Component342::on_click() {}
void Component342::on_hover() {}
void Component342::on_focus() {}
