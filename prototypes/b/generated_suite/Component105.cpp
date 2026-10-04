#include "Component105.h"

#include <cstdint>

struct Component105::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component104* m_child = nullptr;
};

Component105::Component105() : m_impl(new Impl) {}
Component105::~Component105() { delete m_impl; }

float Component105::color() const { return m_impl->m_color; }
float Component105::width() const { return m_impl->m_width; }
void Component105::set_color(float v) { m_impl->m_color = v; }
void Component105::set_opacity(float v) { m_impl->m_opacity = v; }
void Component105::set_width(float v) { m_impl->m_width = v; }
void Component105::set_height(float v) { m_impl->m_height = v; }
void Component105::set_child(Component104* child) { m_impl->m_child = child; }
void Component105::on_click() {}
void Component105::on_hover() {}
void Component105::on_focus() {}
