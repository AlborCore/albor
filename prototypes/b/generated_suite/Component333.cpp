#include "Component333.h"

#include <cstdint>

struct Component333::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component332* m_child = nullptr;
};

Component333::Component333() : m_impl(new Impl) {}
Component333::~Component333() { delete m_impl; }

float Component333::color() const { return m_impl->m_color; }
float Component333::width() const { return m_impl->m_width; }
void Component333::set_color(float v) { m_impl->m_color = v; }
void Component333::set_opacity(float v) { m_impl->m_opacity = v; }
void Component333::set_width(float v) { m_impl->m_width = v; }
void Component333::set_height(float v) { m_impl->m_height = v; }
void Component333::set_child(Component332* child) { m_impl->m_child = child; }
void Component333::on_click() {}
void Component333::on_hover() {}
void Component333::on_focus() {}
