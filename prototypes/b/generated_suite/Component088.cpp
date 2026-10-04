#include "Component088.h"

#include <cstdint>

struct Component088::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component087* m_child = nullptr;
};

Component088::Component088() : m_impl(new Impl) {}
Component088::~Component088() { delete m_impl; }

float Component088::color() const { return m_impl->m_color; }
float Component088::width() const { return m_impl->m_width; }
void Component088::set_color(float v) { m_impl->m_color = v; }
void Component088::set_opacity(float v) { m_impl->m_opacity = v; }
void Component088::set_width(float v) { m_impl->m_width = v; }
void Component088::set_height(float v) { m_impl->m_height = v; }
void Component088::set_child(Component087* child) { m_impl->m_child = child; }
void Component088::on_click() {}
void Component088::on_hover() {}
void Component088::on_focus() {}
