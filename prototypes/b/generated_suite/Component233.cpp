#include "Component233.h"

#include <cstdint>

struct Component233::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component232* m_child = nullptr;
};

Component233::Component233() :
    m_impl(new Impl) {}
Component233::~Component233() { delete m_impl; }

float Component233::color() const { return m_impl->m_color; }
float Component233::width() const { return m_impl->m_width; }
void Component233::set_color(float v) { m_impl->m_color = v; }
void Component233::set_opacity(float v) { m_impl->m_opacity = v; }
void Component233::set_width(float v) { m_impl->m_width = v; }
void Component233::set_height(float v) { m_impl->m_height = v; }
void Component233::set_child(Component232* child) { m_impl->m_child = child; }
void Component233::on_click() {}
void Component233::on_hover() {}
void Component233::on_focus() {}
