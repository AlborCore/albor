#include "Component393.h"

#include <cstdint>

struct Component393::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component392* m_child = nullptr;
};

Component393::Component393() : m_impl(new Impl) {}
Component393::~Component393() { delete m_impl; }

float Component393::color() const { return m_impl->m_color; }
float Component393::width() const { return m_impl->m_width; }
void Component393::set_color(float v) { m_impl->m_color = v; }
void Component393::set_opacity(float v) { m_impl->m_opacity = v; }
void Component393::set_width(float v) { m_impl->m_width = v; }
void Component393::set_height(float v) { m_impl->m_height = v; }
void Component393::set_child(Component392* child) { m_impl->m_child = child; }
void Component393::on_click() {}
void Component393::on_hover() {}
void Component393::on_focus() {}
