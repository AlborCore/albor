#include "Component408.h"

#include <cstdint>

struct Component408::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component407* m_child = nullptr;
};

Component408::Component408() : m_impl(new Impl) {}
Component408::~Component408() { delete m_impl; }

float Component408::color() const { return m_impl->m_color; }
float Component408::width() const { return m_impl->m_width; }
void Component408::set_color(float v) { m_impl->m_color = v; }
void Component408::set_opacity(float v) { m_impl->m_opacity = v; }
void Component408::set_width(float v) { m_impl->m_width = v; }
void Component408::set_height(float v) { m_impl->m_height = v; }
void Component408::set_child(Component407* child) { m_impl->m_child = child; }
void Component408::on_click() {}
void Component408::on_hover() {}
void Component408::on_focus() {}
