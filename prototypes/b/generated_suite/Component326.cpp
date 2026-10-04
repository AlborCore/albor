#include "Component326.h"

#include <cstdint>

struct Component326::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component325* m_child = nullptr;
};

Component326::Component326() :
    m_impl(new Impl) {}
Component326::~Component326() { delete m_impl; }

float Component326::color() const { return m_impl->m_color; }
float Component326::width() const { return m_impl->m_width; }
void Component326::set_color(float v) { m_impl->m_color = v; }
void Component326::set_opacity(float v) { m_impl->m_opacity = v; }
void Component326::set_width(float v) { m_impl->m_width = v; }
void Component326::set_height(float v) { m_impl->m_height = v; }
void Component326::set_child(Component325* child) { m_impl->m_child = child; }
void Component326::on_click() {}
void Component326::on_hover() {}
void Component326::on_focus() {}
