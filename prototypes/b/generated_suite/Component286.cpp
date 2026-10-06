#include "Component286.h"

#include <cstdint>

struct Component286::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component285* m_child = nullptr;
};

Component286::Component286() :
    m_impl(new Impl) {}
Component286::~Component286() { delete m_impl; }

float Component286::color() const { return m_impl->m_color; }
float Component286::width() const { return m_impl->m_width; }
void Component286::set_color(float v) { m_impl->m_color = v; }
void Component286::set_opacity(float v) { m_impl->m_opacity = v; }
void Component286::set_width(float v) { m_impl->m_width = v; }
void Component286::set_height(float v) { m_impl->m_height = v; }
void Component286::set_child(Component285* child) { m_impl->m_child = child; }
void Component286::on_click() {}
void Component286::on_hover() {}
void Component286::on_focus() {}
