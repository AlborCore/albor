#include "Component071.h"

#include <cstdint>

struct Component071::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component070* m_child = nullptr;
};

Component071::Component071() :
    m_impl(new Impl) {}
Component071::~Component071() { delete m_impl; }

float Component071::color() const { return m_impl->m_color; }
float Component071::width() const { return m_impl->m_width; }
void Component071::set_color(float v) { m_impl->m_color = v; }
void Component071::set_opacity(float v) { m_impl->m_opacity = v; }
void Component071::set_width(float v) { m_impl->m_width = v; }
void Component071::set_height(float v) { m_impl->m_height = v; }
void Component071::set_child(Component070* child) { m_impl->m_child = child; }
void Component071::on_click() {}
void Component071::on_hover() {}
void Component071::on_focus() {}
