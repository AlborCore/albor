#include "Component240.h"

#include <cstdint>

struct Component240::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component239* m_child = nullptr;
};

Component240::Component240() :
    m_impl(new Impl) {}
Component240::~Component240() { delete m_impl; }

float Component240::color() const { return m_impl->m_color; }
float Component240::width() const { return m_impl->m_width; }
void Component240::set_color(float v) { m_impl->m_color = v; }
void Component240::set_opacity(float v) { m_impl->m_opacity = v; }
void Component240::set_width(float v) { m_impl->m_width = v; }
void Component240::set_height(float v) { m_impl->m_height = v; }
void Component240::set_child(Component239* child) { m_impl->m_child = child; }
void Component240::on_click() {}
void Component240::on_hover() {}
void Component240::on_focus() {}
