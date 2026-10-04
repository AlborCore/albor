#include "Component209.h"

#include <cstdint>

struct Component209::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component208* m_child = nullptr;
};

Component209::Component209() :
    m_impl(new Impl) {}
Component209::~Component209() { delete m_impl; }

float Component209::color() const { return m_impl->m_color; }
float Component209::width() const { return m_impl->m_width; }
void Component209::set_color(float v) { m_impl->m_color = v; }
void Component209::set_opacity(float v) { m_impl->m_opacity = v; }
void Component209::set_width(float v) { m_impl->m_width = v; }
void Component209::set_height(float v) { m_impl->m_height = v; }
void Component209::set_child(Component208* child) { m_impl->m_child = child; }
void Component209::on_click() {}
void Component209::on_hover() {}
void Component209::on_focus() {}
