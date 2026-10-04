#include "Component318.h"

#include <cstdint>

struct Component318::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component317* m_child = nullptr;
};

Component318::Component318() :
    m_impl(new Impl) {}
Component318::~Component318() { delete m_impl; }

float Component318::color() const { return m_impl->m_color; }
float Component318::width() const { return m_impl->m_width; }
void Component318::set_color(float v) { m_impl->m_color = v; }
void Component318::set_opacity(float v) { m_impl->m_opacity = v; }
void Component318::set_width(float v) { m_impl->m_width = v; }
void Component318::set_height(float v) { m_impl->m_height = v; }
void Component318::set_child(Component317* child) { m_impl->m_child = child; }
void Component318::on_click() {}
void Component318::on_hover() {}
void Component318::on_focus() {}
