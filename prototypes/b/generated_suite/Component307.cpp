#include "Component307.h"

#include <cstdint>

struct Component307::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component306* m_child = nullptr;
};

Component307::Component307() : m_impl(new Impl) {}
Component307::~Component307() { delete m_impl; }

float Component307::color() const { return m_impl->m_color; }
float Component307::width() const { return m_impl->m_width; }
void Component307::set_color(float v) { m_impl->m_color = v; }
void Component307::set_opacity(float v) { m_impl->m_opacity = v; }
void Component307::set_width(float v) { m_impl->m_width = v; }
void Component307::set_height(float v) { m_impl->m_height = v; }
void Component307::set_child(Component306* child) { m_impl->m_child = child; }
void Component307::on_click() {}
void Component307::on_hover() {}
void Component307::on_focus() {}
