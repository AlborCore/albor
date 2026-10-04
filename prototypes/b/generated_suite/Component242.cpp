#include "Component242.h"

#include <cstdint>

struct Component242::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component241* m_child = nullptr;
};

Component242::Component242() : m_impl(new Impl) {}
Component242::~Component242() { delete m_impl; }

float Component242::color() const { return m_impl->m_color; }
float Component242::width() const { return m_impl->m_width; }
void Component242::set_color(float v) { m_impl->m_color = v; }
void Component242::set_opacity(float v) { m_impl->m_opacity = v; }
void Component242::set_width(float v) { m_impl->m_width = v; }
void Component242::set_height(float v) { m_impl->m_height = v; }
void Component242::set_child(Component241* child) { m_impl->m_child = child; }
void Component242::on_click() {}
void Component242::on_hover() {}
void Component242::on_focus() {}
