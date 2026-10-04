#include "Component317.h"

#include <cstdint>

struct Component317::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component316* m_child = nullptr;
};

Component317::Component317() :
    m_impl(new Impl) {}
Component317::~Component317() { delete m_impl; }

float Component317::color() const { return m_impl->m_color; }
float Component317::width() const { return m_impl->m_width; }
void Component317::set_color(float v) { m_impl->m_color = v; }
void Component317::set_opacity(float v) { m_impl->m_opacity = v; }
void Component317::set_width(float v) { m_impl->m_width = v; }
void Component317::set_height(float v) { m_impl->m_height = v; }
void Component317::set_child(Component316* child) { m_impl->m_child = child; }
void Component317::on_click() {}
void Component317::on_hover() {}
void Component317::on_focus() {}
