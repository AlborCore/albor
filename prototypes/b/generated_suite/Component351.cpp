#include "Component351.h"

#include <cstdint>

struct Component351::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component350* m_child = nullptr;
};

Component351::Component351() :
    m_impl(new Impl) {}
Component351::~Component351() { delete m_impl; }

float Component351::color() const { return m_impl->m_color; }
float Component351::width() const { return m_impl->m_width; }
void Component351::set_color(float v) { m_impl->m_color = v; }
void Component351::set_opacity(float v) { m_impl->m_opacity = v; }
void Component351::set_width(float v) { m_impl->m_width = v; }
void Component351::set_height(float v) { m_impl->m_height = v; }
void Component351::set_child(Component350* child) { m_impl->m_child = child; }
void Component351::on_click() {}
void Component351::on_hover() {}
void Component351::on_focus() {}
