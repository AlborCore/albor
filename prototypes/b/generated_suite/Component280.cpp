#include "Component280.h"

#include <cstdint>

struct Component280::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component279* m_child = nullptr;
};

Component280::Component280() :
    m_impl(new Impl) {}
Component280::~Component280() { delete m_impl; }

float Component280::color() const { return m_impl->m_color; }
float Component280::width() const { return m_impl->m_width; }
void Component280::set_color(float v) { m_impl->m_color = v; }
void Component280::set_opacity(float v) { m_impl->m_opacity = v; }
void Component280::set_width(float v) { m_impl->m_width = v; }
void Component280::set_height(float v) { m_impl->m_height = v; }
void Component280::set_child(Component279* child) { m_impl->m_child = child; }
void Component280::on_click() {}
void Component280::on_hover() {}
void Component280::on_focus() {}
