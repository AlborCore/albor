#include "Component499.h"

#include <cstdint>

struct Component499::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component498* m_child = nullptr;
};

Component499::Component499() :
    m_impl(new Impl) {}
Component499::~Component499() { delete m_impl; }

float Component499::color() const { return m_impl->m_color; }
float Component499::width() const { return m_impl->m_width; }
void Component499::set_color(float v) { m_impl->m_color = v; }
void Component499::set_opacity(float v) { m_impl->m_opacity = v; }
void Component499::set_width(float v) { m_impl->m_width = v; }
void Component499::set_height(float v) { m_impl->m_height = v; }
void Component499::set_child(Component498* child) { m_impl->m_child = child; }
void Component499::on_click() {}
void Component499::on_hover() {}
void Component499::on_focus() {}
