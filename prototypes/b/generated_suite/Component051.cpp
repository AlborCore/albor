#include "Component051.h"

#include <cstdint>

struct Component051::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component050* m_child = nullptr;
};

Component051::Component051() :
    m_impl(new Impl) {}
Component051::~Component051() { delete m_impl; }

float Component051::color() const { return m_impl->m_color; }
float Component051::width() const { return m_impl->m_width; }
void Component051::set_color(float v) { m_impl->m_color = v; }
void Component051::set_opacity(float v) { m_impl->m_opacity = v; }
void Component051::set_width(float v) { m_impl->m_width = v; }
void Component051::set_height(float v) { m_impl->m_height = v; }
void Component051::set_child(Component050* child) { m_impl->m_child = child; }
void Component051::on_click() {}
void Component051::on_hover() {}
void Component051::on_focus() {}
