#include "Component287.h"

#include <cstdint>

struct Component287::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component286* m_child = nullptr;
};

Component287::Component287() : m_impl(new Impl) {}
Component287::~Component287() { delete m_impl; }

float Component287::color() const { return m_impl->m_color; }
float Component287::width() const { return m_impl->m_width; }
void Component287::set_color(float v) { m_impl->m_color = v; }
void Component287::set_opacity(float v) { m_impl->m_opacity = v; }
void Component287::set_width(float v) { m_impl->m_width = v; }
void Component287::set_height(float v) { m_impl->m_height = v; }
void Component287::set_child(Component286* child) { m_impl->m_child = child; }
void Component287::on_click() {}
void Component287::on_hover() {}
void Component287::on_focus() {}
