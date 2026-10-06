#include "Component202.h"

#include <cstdint>

struct Component202::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component201* m_child = nullptr;
};

Component202::Component202() :
    m_impl(new Impl) {}
Component202::~Component202() { delete m_impl; }

float Component202::color() const { return m_impl->m_color; }
float Component202::width() const { return m_impl->m_width; }
void Component202::set_color(float v) { m_impl->m_color = v; }
void Component202::set_opacity(float v) { m_impl->m_opacity = v; }
void Component202::set_width(float v) { m_impl->m_width = v; }
void Component202::set_height(float v) { m_impl->m_height = v; }
void Component202::set_child(Component201* child) { m_impl->m_child = child; }
void Component202::on_click() {}
void Component202::on_hover() {}
void Component202::on_focus() {}
