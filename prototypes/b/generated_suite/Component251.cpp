#include "Component251.h"

#include <cstdint>

struct Component251::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component250* m_child = nullptr;
};

Component251::Component251() :
    m_impl(new Impl) {}
Component251::~Component251() { delete m_impl; }

float Component251::color() const { return m_impl->m_color; }
float Component251::width() const { return m_impl->m_width; }
void Component251::set_color(float v) { m_impl->m_color = v; }
void Component251::set_opacity(float v) { m_impl->m_opacity = v; }
void Component251::set_width(float v) { m_impl->m_width = v; }
void Component251::set_height(float v) { m_impl->m_height = v; }
void Component251::set_child(Component250* child) { m_impl->m_child = child; }
void Component251::on_click() {}
void Component251::on_hover() {}
void Component251::on_focus() {}
