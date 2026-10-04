#include "Component426.h"

#include <cstdint>

struct Component426::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component425* m_child = nullptr;
};

Component426::Component426() : m_impl(new Impl) {}
Component426::~Component426() { delete m_impl; }

float Component426::color() const { return m_impl->m_color; }
float Component426::width() const { return m_impl->m_width; }
void Component426::set_color(float v) { m_impl->m_color = v; }
void Component426::set_opacity(float v) { m_impl->m_opacity = v; }
void Component426::set_width(float v) { m_impl->m_width = v; }
void Component426::set_height(float v) { m_impl->m_height = v; }
void Component426::set_child(Component425* child) { m_impl->m_child = child; }
void Component426::on_click() {}
void Component426::on_hover() {}
void Component426::on_focus() {}
