#include "Component403.h"

#include <cstdint>

struct Component403::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component402* m_child = nullptr;
};

Component403::Component403() : m_impl(new Impl) {}
Component403::~Component403() { delete m_impl; }

float Component403::color() const { return m_impl->m_color; }
float Component403::width() const { return m_impl->m_width; }
void Component403::set_color(float v) { m_impl->m_color = v; }
void Component403::set_opacity(float v) { m_impl->m_opacity = v; }
void Component403::set_width(float v) { m_impl->m_width = v; }
void Component403::set_height(float v) { m_impl->m_height = v; }
void Component403::set_child(Component402* child) { m_impl->m_child = child; }
void Component403::on_click() {}
void Component403::on_hover() {}
void Component403::on_focus() {}
