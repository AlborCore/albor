#include "Component049.h"

#include <cstdint>

struct Component049::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component048* m_child = nullptr;
};

Component049::Component049() : m_impl(new Impl) {}
Component049::~Component049() { delete m_impl; }

float Component049::color() const { return m_impl->m_color; }
float Component049::width() const { return m_impl->m_width; }
void Component049::set_color(float v) { m_impl->m_color = v; }
void Component049::set_opacity(float v) { m_impl->m_opacity = v; }
void Component049::set_width(float v) { m_impl->m_width = v; }
void Component049::set_height(float v) { m_impl->m_height = v; }
void Component049::set_child(Component048* child) { m_impl->m_child = child; }
void Component049::on_click() {}
void Component049::on_hover() {}
void Component049::on_focus() {}
