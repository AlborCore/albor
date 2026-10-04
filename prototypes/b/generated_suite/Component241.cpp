#include "Component241.h"

#include <cstdint>

struct Component241::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component240* m_child = nullptr;
};

Component241::Component241() : m_impl(new Impl) {}
Component241::~Component241() { delete m_impl; }

float Component241::color() const { return m_impl->m_color; }
float Component241::width() const { return m_impl->m_width; }
void Component241::set_color(float v) { m_impl->m_color = v; }
void Component241::set_opacity(float v) { m_impl->m_opacity = v; }
void Component241::set_width(float v) { m_impl->m_width = v; }
void Component241::set_height(float v) { m_impl->m_height = v; }
void Component241::set_child(Component240* child) { m_impl->m_child = child; }
void Component241::on_click() {}
void Component241::on_hover() {}
void Component241::on_focus() {}
