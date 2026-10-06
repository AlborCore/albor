#include "Component180.h"

#include <cstdint>

struct Component180::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component179* m_child = nullptr;
};

Component180::Component180() :
    m_impl(new Impl) {}
Component180::~Component180() { delete m_impl; }

float Component180::color() const { return m_impl->m_color; }
float Component180::width() const { return m_impl->m_width; }
void Component180::set_color(float v) { m_impl->m_color = v; }
void Component180::set_opacity(float v) { m_impl->m_opacity = v; }
void Component180::set_width(float v) { m_impl->m_width = v; }
void Component180::set_height(float v) { m_impl->m_height = v; }
void Component180::set_child(Component179* child) { m_impl->m_child = child; }
void Component180::on_click() {}
void Component180::on_hover() {}
void Component180::on_focus() {}
