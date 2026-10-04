#include "Component115.h"

#include <cstdint>

struct Component115::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component114* m_child = nullptr;
};

Component115::Component115() : m_impl(new Impl) {}
Component115::~Component115() { delete m_impl; }

float Component115::color() const { return m_impl->m_color; }
float Component115::width() const { return m_impl->m_width; }
void Component115::set_color(float v) { m_impl->m_color = v; }
void Component115::set_opacity(float v) { m_impl->m_opacity = v; }
void Component115::set_width(float v) { m_impl->m_width = v; }
void Component115::set_height(float v) { m_impl->m_height = v; }
void Component115::set_child(Component114* child) { m_impl->m_child = child; }
void Component115::on_click() {}
void Component115::on_hover() {}
void Component115::on_focus() {}
