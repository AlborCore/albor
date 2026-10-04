#include "Component309.h"

#include <cstdint>

struct Component309::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component308* m_child = nullptr;
};

Component309::Component309() : m_impl(new Impl) {}
Component309::~Component309() { delete m_impl; }

float Component309::color() const { return m_impl->m_color; }
float Component309::width() const { return m_impl->m_width; }
void Component309::set_color(float v) { m_impl->m_color = v; }
void Component309::set_opacity(float v) { m_impl->m_opacity = v; }
void Component309::set_width(float v) { m_impl->m_width = v; }
void Component309::set_height(float v) { m_impl->m_height = v; }
void Component309::set_child(Component308* child) { m_impl->m_child = child; }
void Component309::on_click() {}
void Component309::on_hover() {}
void Component309::on_focus() {}
