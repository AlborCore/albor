#include "Component137.h"

#include <cstdint>

struct Component137::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component136* m_child = nullptr;
};

Component137::Component137() :
    m_impl(new Impl) {}
Component137::~Component137() { delete m_impl; }

float Component137::color() const { return m_impl->m_color; }
float Component137::width() const { return m_impl->m_width; }
void Component137::set_color(float v) { m_impl->m_color = v; }
void Component137::set_opacity(float v) { m_impl->m_opacity = v; }
void Component137::set_width(float v) { m_impl->m_width = v; }
void Component137::set_height(float v) { m_impl->m_height = v; }
void Component137::set_child(Component136* child) { m_impl->m_child = child; }
void Component137::on_click() {}
void Component137::on_hover() {}
void Component137::on_focus() {}
