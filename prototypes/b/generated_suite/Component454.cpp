#include "Component454.h"

#include <cstdint>

struct Component454::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component453* m_child = nullptr;
};

Component454::Component454() :
    m_impl(new Impl) {}
Component454::~Component454() { delete m_impl; }

float Component454::color() const { return m_impl->m_color; }
float Component454::width() const { return m_impl->m_width; }
void Component454::set_color(float v) { m_impl->m_color = v; }
void Component454::set_opacity(float v) { m_impl->m_opacity = v; }
void Component454::set_width(float v) { m_impl->m_width = v; }
void Component454::set_height(float v) { m_impl->m_height = v; }
void Component454::set_child(Component453* child) { m_impl->m_child = child; }
void Component454::on_click() {}
void Component454::on_hover() {}
void Component454::on_focus() {}
