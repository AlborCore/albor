#include "Component250.h"

#include <cstdint>

struct Component250::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component249* m_child = nullptr;
};

Component250::Component250() :
    m_impl(new Impl) {}
Component250::~Component250() { delete m_impl; }

float Component250::color() const { return m_impl->m_color; }
float Component250::width() const { return m_impl->m_width; }
void Component250::set_color(float v) { m_impl->m_color = v; }
void Component250::set_opacity(float v) { m_impl->m_opacity = v; }
void Component250::set_width(float v) { m_impl->m_width = v; }
void Component250::set_height(float v) { m_impl->m_height = v; }
void Component250::set_child(Component249* child) { m_impl->m_child = child; }
void Component250::on_click() {}
void Component250::on_hover() {}
void Component250::on_focus() {}
