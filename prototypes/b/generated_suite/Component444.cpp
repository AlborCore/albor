#include "Component444.h"

#include <cstdint>

struct Component444::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component443* m_child = nullptr;
};

Component444::Component444() : m_impl(new Impl) {}
Component444::~Component444() { delete m_impl; }

float Component444::color() const { return m_impl->m_color; }
float Component444::width() const { return m_impl->m_width; }
void Component444::set_color(float v) { m_impl->m_color = v; }
void Component444::set_opacity(float v) { m_impl->m_opacity = v; }
void Component444::set_width(float v) { m_impl->m_width = v; }
void Component444::set_height(float v) { m_impl->m_height = v; }
void Component444::set_child(Component443* child) { m_impl->m_child = child; }
void Component444::on_click() {}
void Component444::on_hover() {}
void Component444::on_focus() {}
