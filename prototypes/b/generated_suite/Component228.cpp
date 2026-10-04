#include "Component228.h"

#include <cstdint>

struct Component228::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component227* m_child = nullptr;
};

Component228::Component228() : m_impl(new Impl) {}
Component228::~Component228() { delete m_impl; }

float Component228::color() const { return m_impl->m_color; }
float Component228::width() const { return m_impl->m_width; }
void Component228::set_color(float v) { m_impl->m_color = v; }
void Component228::set_opacity(float v) { m_impl->m_opacity = v; }
void Component228::set_width(float v) { m_impl->m_width = v; }
void Component228::set_height(float v) { m_impl->m_height = v; }
void Component228::set_child(Component227* child) { m_impl->m_child = child; }
void Component228::on_click() {}
void Component228::on_hover() {}
void Component228::on_focus() {}
