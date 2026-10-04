#include "Component062.h"

#include <cstdint>

struct Component062::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component061* m_child = nullptr;
};

Component062::Component062() : m_impl(new Impl) {}
Component062::~Component062() { delete m_impl; }

float Component062::color() const { return m_impl->m_color; }
float Component062::width() const { return m_impl->m_width; }
void Component062::set_color(float v) { m_impl->m_color = v; }
void Component062::set_opacity(float v) { m_impl->m_opacity = v; }
void Component062::set_width(float v) { m_impl->m_width = v; }
void Component062::set_height(float v) { m_impl->m_height = v; }
void Component062::set_child(Component061* child) { m_impl->m_child = child; }
void Component062::on_click() {}
void Component062::on_hover() {}
void Component062::on_focus() {}
