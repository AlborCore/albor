#include "Component433.h"

#include <cstdint>

struct Component433::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component432* m_child = nullptr;
};

Component433::Component433() : m_impl(new Impl) {}
Component433::~Component433() { delete m_impl; }

float Component433::color() const { return m_impl->m_color; }
float Component433::width() const { return m_impl->m_width; }
void Component433::set_color(float v) { m_impl->m_color = v; }
void Component433::set_opacity(float v) { m_impl->m_opacity = v; }
void Component433::set_width(float v) { m_impl->m_width = v; }
void Component433::set_height(float v) { m_impl->m_height = v; }
void Component433::set_child(Component432* child) { m_impl->m_child = child; }
void Component433::on_click() {}
void Component433::on_hover() {}
void Component433::on_focus() {}
