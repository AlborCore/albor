#include "Component122.h"

#include <cstdint>

struct Component122::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component121* m_child = nullptr;
};

Component122::Component122() : m_impl(new Impl) {}
Component122::~Component122() { delete m_impl; }

float Component122::color() const { return m_impl->m_color; }
float Component122::width() const { return m_impl->m_width; }
void Component122::set_color(float v) { m_impl->m_color = v; }
void Component122::set_opacity(float v) { m_impl->m_opacity = v; }
void Component122::set_width(float v) { m_impl->m_width = v; }
void Component122::set_height(float v) { m_impl->m_height = v; }
void Component122::set_child(Component121* child) { m_impl->m_child = child; }
void Component122::on_click() {}
void Component122::on_hover() {}
void Component122::on_focus() {}
