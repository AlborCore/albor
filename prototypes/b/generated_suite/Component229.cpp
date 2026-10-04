#include "Component229.h"

#include <cstdint>

struct Component229::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component228* m_child = nullptr;
};

Component229::Component229() : m_impl(new Impl) {}
Component229::~Component229() { delete m_impl; }

float Component229::color() const { return m_impl->m_color; }
float Component229::width() const { return m_impl->m_width; }
void Component229::set_color(float v) { m_impl->m_color = v; }
void Component229::set_opacity(float v) { m_impl->m_opacity = v; }
void Component229::set_width(float v) { m_impl->m_width = v; }
void Component229::set_height(float v) { m_impl->m_height = v; }
void Component229::set_child(Component228* child) { m_impl->m_child = child; }
void Component229::on_click() {}
void Component229::on_hover() {}
void Component229::on_focus() {}
