#include "Component018.h"

#include <cstdint>

struct Component018::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component017* m_child = nullptr;
};

Component018::Component018() :
    m_impl(new Impl) {}
Component018::~Component018() { delete m_impl; }

float Component018::color() const { return m_impl->m_color; }
float Component018::width() const { return m_impl->m_width; }
void Component018::set_color(float v) { m_impl->m_color = v; }
void Component018::set_opacity(float v) { m_impl->m_opacity = v; }
void Component018::set_width(float v) { m_impl->m_width = v; }
void Component018::set_height(float v) { m_impl->m_height = v; }
void Component018::set_child(Component017* child) { m_impl->m_child = child; }
void Component018::on_click() {}
void Component018::on_hover() {}
void Component018::on_focus() {}
