#include "Component077.h"

#include <cstdint>

struct Component077::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component076* m_child = nullptr;
};

Component077::Component077() :
    m_impl(new Impl) {}
Component077::~Component077() { delete m_impl; }

float Component077::color() const { return m_impl->m_color; }
float Component077::width() const { return m_impl->m_width; }
void Component077::set_color(float v) { m_impl->m_color = v; }
void Component077::set_opacity(float v) { m_impl->m_opacity = v; }
void Component077::set_width(float v) { m_impl->m_width = v; }
void Component077::set_height(float v) { m_impl->m_height = v; }
void Component077::set_child(Component076* child) { m_impl->m_child = child; }
void Component077::on_click() {}
void Component077::on_hover() {}
void Component077::on_focus() {}
