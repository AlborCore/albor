#include "Component133.h"

#include <cstdint>

struct Component133::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component132* m_child = nullptr;
};

Component133::Component133() :
    m_impl(new Impl) {}
Component133::~Component133() { delete m_impl; }

float Component133::color() const { return m_impl->m_color; }
float Component133::width() const { return m_impl->m_width; }
void Component133::set_color(float v) { m_impl->m_color = v; }
void Component133::set_opacity(float v) { m_impl->m_opacity = v; }
void Component133::set_width(float v) { m_impl->m_width = v; }
void Component133::set_height(float v) { m_impl->m_height = v; }
void Component133::set_child(Component132* child) { m_impl->m_child = child; }
void Component133::on_click() {}
void Component133::on_hover() {}
void Component133::on_focus() {}
