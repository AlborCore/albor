#include "Component022.h"

#include <cstdint>

struct Component022::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component021* m_child = nullptr;
};

Component022::Component022() : m_impl(new Impl) {}
Component022::~Component022() { delete m_impl; }

float Component022::color() const { return m_impl->m_color; }
float Component022::width() const { return m_impl->m_width; }
void Component022::set_color(float v) { m_impl->m_color = v; }
void Component022::set_opacity(float v) { m_impl->m_opacity = v; }
void Component022::set_width(float v) { m_impl->m_width = v; }
void Component022::set_height(float v) { m_impl->m_height = v; }
void Component022::set_child(Component021* child) { m_impl->m_child = child; }
void Component022::on_click() {}
void Component022::on_hover() {}
void Component022::on_focus() {}
