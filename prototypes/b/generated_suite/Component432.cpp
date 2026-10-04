#include "Component432.h"

#include <cstdint>

struct Component432::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component431* m_child = nullptr;
};

Component432::Component432() : m_impl(new Impl) {}
Component432::~Component432() { delete m_impl; }

float Component432::color() const { return m_impl->m_color; }
float Component432::width() const { return m_impl->m_width; }
void Component432::set_color(float v) { m_impl->m_color = v; }
void Component432::set_opacity(float v) { m_impl->m_opacity = v; }
void Component432::set_width(float v) { m_impl->m_width = v; }
void Component432::set_height(float v) { m_impl->m_height = v; }
void Component432::set_child(Component431* child) { m_impl->m_child = child; }
void Component432::on_click() {}
void Component432::on_hover() {}
void Component432::on_focus() {}
