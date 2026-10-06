#include "Component304.h"

#include <cstdint>

struct Component304::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component303* m_child = nullptr;
};

Component304::Component304() :
    m_impl(new Impl) {}
Component304::~Component304() { delete m_impl; }

float Component304::color() const { return m_impl->m_color; }
float Component304::width() const { return m_impl->m_width; }
void Component304::set_color(float v) { m_impl->m_color = v; }
void Component304::set_opacity(float v) { m_impl->m_opacity = v; }
void Component304::set_width(float v) { m_impl->m_width = v; }
void Component304::set_height(float v) { m_impl->m_height = v; }
void Component304::set_child(Component303* child) { m_impl->m_child = child; }
void Component304::on_click() {}
void Component304::on_hover() {}
void Component304::on_focus() {}
