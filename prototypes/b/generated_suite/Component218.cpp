#include "Component218.h"

#include <cstdint>

struct Component218::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component217* m_child = nullptr;
};

Component218::Component218() :
    m_impl(new Impl) {}
Component218::~Component218() { delete m_impl; }

float Component218::color() const { return m_impl->m_color; }
float Component218::width() const { return m_impl->m_width; }
void Component218::set_color(float v) { m_impl->m_color = v; }
void Component218::set_opacity(float v) { m_impl->m_opacity = v; }
void Component218::set_width(float v) { m_impl->m_width = v; }
void Component218::set_height(float v) { m_impl->m_height = v; }
void Component218::set_child(Component217* child) { m_impl->m_child = child; }
void Component218::on_click() {}
void Component218::on_hover() {}
void Component218::on_focus() {}
