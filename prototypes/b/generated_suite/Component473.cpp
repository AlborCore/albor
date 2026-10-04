#include "Component473.h"

#include <cstdint>

struct Component473::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component472* m_child = nullptr;
};

Component473::Component473() : m_impl(new Impl) {}
Component473::~Component473() { delete m_impl; }

float Component473::color() const { return m_impl->m_color; }
float Component473::width() const { return m_impl->m_width; }
void Component473::set_color(float v) { m_impl->m_color = v; }
void Component473::set_opacity(float v) { m_impl->m_opacity = v; }
void Component473::set_width(float v) { m_impl->m_width = v; }
void Component473::set_height(float v) { m_impl->m_height = v; }
void Component473::set_child(Component472* child) { m_impl->m_child = child; }
void Component473::on_click() {}
void Component473::on_hover() {}
void Component473::on_focus() {}
