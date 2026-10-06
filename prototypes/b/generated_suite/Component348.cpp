#include "Component348.h"

#include <cstdint>

struct Component348::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component347* m_child = nullptr;
};

Component348::Component348() :
    m_impl(new Impl) {}
Component348::~Component348() { delete m_impl; }

float Component348::color() const { return m_impl->m_color; }
float Component348::width() const { return m_impl->m_width; }
void Component348::set_color(float v) { m_impl->m_color = v; }
void Component348::set_opacity(float v) { m_impl->m_opacity = v; }
void Component348::set_width(float v) { m_impl->m_width = v; }
void Component348::set_height(float v) { m_impl->m_height = v; }
void Component348::set_child(Component347* child) { m_impl->m_child = child; }
void Component348::on_click() {}
void Component348::on_hover() {}
void Component348::on_focus() {}
