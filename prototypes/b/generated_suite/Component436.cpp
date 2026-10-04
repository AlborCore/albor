#include "Component436.h"

#include <cstdint>

struct Component436::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component435* m_child = nullptr;
};

Component436::Component436() : m_impl(new Impl) {}
Component436::~Component436() { delete m_impl; }

float Component436::color() const { return m_impl->m_color; }
float Component436::width() const { return m_impl->m_width; }
void Component436::set_color(float v) { m_impl->m_color = v; }
void Component436::set_opacity(float v) { m_impl->m_opacity = v; }
void Component436::set_width(float v) { m_impl->m_width = v; }
void Component436::set_height(float v) { m_impl->m_height = v; }
void Component436::set_child(Component435* child) { m_impl->m_child = child; }
void Component436::on_click() {}
void Component436::on_hover() {}
void Component436::on_focus() {}
