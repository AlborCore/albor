#include "Component173.h"

#include <cstdint>

struct Component173::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component172* m_child = nullptr;
};

Component173::Component173() :
    m_impl(new Impl) {}
Component173::~Component173() { delete m_impl; }

float Component173::color() const { return m_impl->m_color; }
float Component173::width() const { return m_impl->m_width; }
void Component173::set_color(float v) { m_impl->m_color = v; }
void Component173::set_opacity(float v) { m_impl->m_opacity = v; }
void Component173::set_width(float v) { m_impl->m_width = v; }
void Component173::set_height(float v) { m_impl->m_height = v; }
void Component173::set_child(Component172* child) { m_impl->m_child = child; }
void Component173::on_click() {}
void Component173::on_hover() {}
void Component173::on_focus() {}
