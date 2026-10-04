#include "Component255.h"

#include <cstdint>

struct Component255::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component254* m_child = nullptr;
};

Component255::Component255() :
    m_impl(new Impl) {}
Component255::~Component255() { delete m_impl; }

float Component255::color() const { return m_impl->m_color; }
float Component255::width() const { return m_impl->m_width; }
void Component255::set_color(float v) { m_impl->m_color = v; }
void Component255::set_opacity(float v) { m_impl->m_opacity = v; }
void Component255::set_width(float v) { m_impl->m_width = v; }
void Component255::set_height(float v) { m_impl->m_height = v; }
void Component255::set_child(Component254* child) { m_impl->m_child = child; }
void Component255::on_click() {}
void Component255::on_hover() {}
void Component255::on_focus() {}
