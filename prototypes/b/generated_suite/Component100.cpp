#include "Component100.h"

#include <cstdint>

struct Component100::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component099* m_child = nullptr;
};

Component100::Component100() : m_impl(new Impl) {}
Component100::~Component100() { delete m_impl; }

float Component100::color() const { return m_impl->m_color; }
float Component100::width() const { return m_impl->m_width; }
void Component100::set_color(float v) { m_impl->m_color = v; }
void Component100::set_opacity(float v) { m_impl->m_opacity = v; }
void Component100::set_width(float v) { m_impl->m_width = v; }
void Component100::set_height(float v) { m_impl->m_height = v; }
void Component100::set_child(Component099* child) { m_impl->m_child = child; }
void Component100::on_click() {}
void Component100::on_hover() {}
void Component100::on_focus() {}
