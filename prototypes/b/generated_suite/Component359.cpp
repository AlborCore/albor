#include "Component359.h"

#include <cstdint>

struct Component359::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component358* m_child = nullptr;
};

Component359::Component359() : m_impl(new Impl) {}
Component359::~Component359() { delete m_impl; }

float Component359::color() const { return m_impl->m_color; }
float Component359::width() const { return m_impl->m_width; }
void Component359::set_color(float v) { m_impl->m_color = v; }
void Component359::set_opacity(float v) { m_impl->m_opacity = v; }
void Component359::set_width(float v) { m_impl->m_width = v; }
void Component359::set_height(float v) { m_impl->m_height = v; }
void Component359::set_child(Component358* child) { m_impl->m_child = child; }
void Component359::on_click() {}
void Component359::on_hover() {}
void Component359::on_focus() {}
