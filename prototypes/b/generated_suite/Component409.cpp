#include "Component409.h"

#include <cstdint>

struct Component409::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component408* m_child = nullptr;
};

Component409::Component409() : m_impl(new Impl) {}
Component409::~Component409() { delete m_impl; }

float Component409::color() const { return m_impl->m_color; }
float Component409::width() const { return m_impl->m_width; }
void Component409::set_color(float v) { m_impl->m_color = v; }
void Component409::set_opacity(float v) { m_impl->m_opacity = v; }
void Component409::set_width(float v) { m_impl->m_width = v; }
void Component409::set_height(float v) { m_impl->m_height = v; }
void Component409::set_child(Component408* child) { m_impl->m_child = child; }
void Component409::on_click() {}
void Component409::on_hover() {}
void Component409::on_focus() {}
