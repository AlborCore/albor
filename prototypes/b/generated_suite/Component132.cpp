#include "Component132.h"

#include <cstdint>

struct Component132::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component131* m_child = nullptr;
};

Component132::Component132() : m_impl(new Impl) {}
Component132::~Component132() { delete m_impl; }

float Component132::color() const { return m_impl->m_color; }
float Component132::width() const { return m_impl->m_width; }
void Component132::set_color(float v) { m_impl->m_color = v; }
void Component132::set_opacity(float v) { m_impl->m_opacity = v; }
void Component132::set_width(float v) { m_impl->m_width = v; }
void Component132::set_height(float v) { m_impl->m_height = v; }
void Component132::set_child(Component131* child) { m_impl->m_child = child; }
void Component132::on_click() {}
void Component132::on_hover() {}
void Component132::on_focus() {}
