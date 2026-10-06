#include "Component339.h"

#include <cstdint>

struct Component339::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component338* m_child = nullptr;
};

Component339::Component339() :
    m_impl(new Impl) {}
Component339::~Component339() { delete m_impl; }

float Component339::color() const { return m_impl->m_color; }
float Component339::width() const { return m_impl->m_width; }
void Component339::set_color(float v) { m_impl->m_color = v; }
void Component339::set_opacity(float v) { m_impl->m_opacity = v; }
void Component339::set_width(float v) { m_impl->m_width = v; }
void Component339::set_height(float v) { m_impl->m_height = v; }
void Component339::set_child(Component338* child) { m_impl->m_child = child; }
void Component339::on_click() {}
void Component339::on_hover() {}
void Component339::on_focus() {}
