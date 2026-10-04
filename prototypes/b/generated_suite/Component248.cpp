#include "Component248.h"

#include <cstdint>

struct Component248::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component247* m_child = nullptr;
};

Component248::Component248() :
    m_impl(new Impl) {}
Component248::~Component248() { delete m_impl; }

float Component248::color() const { return m_impl->m_color; }
float Component248::width() const { return m_impl->m_width; }
void Component248::set_color(float v) { m_impl->m_color = v; }
void Component248::set_opacity(float v) { m_impl->m_opacity = v; }
void Component248::set_width(float v) { m_impl->m_width = v; }
void Component248::set_height(float v) { m_impl->m_height = v; }
void Component248::set_child(Component247* child) { m_impl->m_child = child; }
void Component248::on_click() {}
void Component248::on_hover() {}
void Component248::on_focus() {}
