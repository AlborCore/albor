#include "Component462.h"

#include <cstdint>

struct Component462::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component461* m_child = nullptr;
};

Component462::Component462() :
    m_impl(new Impl) {}
Component462::~Component462() { delete m_impl; }

float Component462::color() const { return m_impl->m_color; }
float Component462::width() const { return m_impl->m_width; }
void Component462::set_color(float v) { m_impl->m_color = v; }
void Component462::set_opacity(float v) { m_impl->m_opacity = v; }
void Component462::set_width(float v) { m_impl->m_width = v; }
void Component462::set_height(float v) { m_impl->m_height = v; }
void Component462::set_child(Component461* child) { m_impl->m_child = child; }
void Component462::on_click() {}
void Component462::on_hover() {}
void Component462::on_focus() {}
