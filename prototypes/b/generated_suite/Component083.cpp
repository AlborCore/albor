#include "Component083.h"

#include <cstdint>

struct Component083::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component082* m_child = nullptr;
};

Component083::Component083() :
    m_impl(new Impl) {}
Component083::~Component083() { delete m_impl; }

float Component083::color() const { return m_impl->m_color; }
float Component083::width() const { return m_impl->m_width; }
void Component083::set_color(float v) { m_impl->m_color = v; }
void Component083::set_opacity(float v) { m_impl->m_opacity = v; }
void Component083::set_width(float v) { m_impl->m_width = v; }
void Component083::set_height(float v) { m_impl->m_height = v; }
void Component083::set_child(Component082* child) { m_impl->m_child = child; }
void Component083::on_click() {}
void Component083::on_hover() {}
void Component083::on_focus() {}
