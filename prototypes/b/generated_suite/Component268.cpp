#include "Component268.h"

#include <cstdint>

struct Component268::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component267* m_child = nullptr;
};

Component268::Component268() : m_impl(new Impl) {}
Component268::~Component268() { delete m_impl; }

float Component268::color() const { return m_impl->m_color; }
float Component268::width() const { return m_impl->m_width; }
void Component268::set_color(float v) { m_impl->m_color = v; }
void Component268::set_opacity(float v) { m_impl->m_opacity = v; }
void Component268::set_width(float v) { m_impl->m_width = v; }
void Component268::set_height(float v) { m_impl->m_height = v; }
void Component268::set_child(Component267* child) { m_impl->m_child = child; }
void Component268::on_click() {}
void Component268::on_hover() {}
void Component268::on_focus() {}
