#include "Component481.h"

#include <cstdint>

struct Component481::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component480* m_child = nullptr;
};

Component481::Component481() :
    m_impl(new Impl) {}
Component481::~Component481() { delete m_impl; }

float Component481::color() const { return m_impl->m_color; }
float Component481::width() const { return m_impl->m_width; }
void Component481::set_color(float v) { m_impl->m_color = v; }
void Component481::set_opacity(float v) { m_impl->m_opacity = v; }
void Component481::set_width(float v) { m_impl->m_width = v; }
void Component481::set_height(float v) { m_impl->m_height = v; }
void Component481::set_child(Component480* child) { m_impl->m_child = child; }
void Component481::on_click() {}
void Component481::on_hover() {}
void Component481::on_focus() {}
