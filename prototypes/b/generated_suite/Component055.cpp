#include "Component055.h"

#include <cstdint>

struct Component055::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component054* m_child = nullptr;
};

Component055::Component055() : m_impl(new Impl) {}
Component055::~Component055() { delete m_impl; }

float Component055::color() const { return m_impl->m_color; }
float Component055::width() const { return m_impl->m_width; }
void Component055::set_color(float v) { m_impl->m_color = v; }
void Component055::set_opacity(float v) { m_impl->m_opacity = v; }
void Component055::set_width(float v) { m_impl->m_width = v; }
void Component055::set_height(float v) { m_impl->m_height = v; }
void Component055::set_child(Component054* child) { m_impl->m_child = child; }
void Component055::on_click() {}
void Component055::on_hover() {}
void Component055::on_focus() {}
