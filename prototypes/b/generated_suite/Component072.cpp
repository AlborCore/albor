#include "Component072.h"

#include <cstdint>

struct Component072::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component071* m_child = nullptr;
};

Component072::Component072() :
    m_impl(new Impl) {}
Component072::~Component072() { delete m_impl; }

float Component072::color() const { return m_impl->m_color; }
float Component072::width() const { return m_impl->m_width; }
void Component072::set_color(float v) { m_impl->m_color = v; }
void Component072::set_opacity(float v) { m_impl->m_opacity = v; }
void Component072::set_width(float v) { m_impl->m_width = v; }
void Component072::set_height(float v) { m_impl->m_height = v; }
void Component072::set_child(Component071* child) { m_impl->m_child = child; }
void Component072::on_click() {}
void Component072::on_hover() {}
void Component072::on_focus() {}
