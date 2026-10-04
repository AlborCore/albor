#include "Component201.h"

#include <cstdint>

struct Component201::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component200* m_child = nullptr;
};

Component201::Component201() : m_impl(new Impl) {}
Component201::~Component201() { delete m_impl; }

float Component201::color() const { return m_impl->m_color; }
float Component201::width() const { return m_impl->m_width; }
void Component201::set_color(float v) { m_impl->m_color = v; }
void Component201::set_opacity(float v) { m_impl->m_opacity = v; }
void Component201::set_width(float v) { m_impl->m_width = v; }
void Component201::set_height(float v) { m_impl->m_height = v; }
void Component201::set_child(Component200* child) { m_impl->m_child = child; }
void Component201::on_click() {}
void Component201::on_hover() {}
void Component201::on_focus() {}
