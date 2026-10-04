#include "Component222.h"

#include <cstdint>

struct Component222::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component221* m_child = nullptr;
};

Component222::Component222() :
    m_impl(new Impl) {}
Component222::~Component222() { delete m_impl; }

float Component222::color() const { return m_impl->m_color; }
float Component222::width() const { return m_impl->m_width; }
void Component222::set_color(float v) { m_impl->m_color = v; }
void Component222::set_opacity(float v) { m_impl->m_opacity = v; }
void Component222::set_width(float v) { m_impl->m_width = v; }
void Component222::set_height(float v) { m_impl->m_height = v; }
void Component222::set_child(Component221* child) { m_impl->m_child = child; }
void Component222::on_click() {}
void Component222::on_hover() {}
void Component222::on_focus() {}
