#include "Component350.h"

#include <cstdint>

struct Component350::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component349* m_child = nullptr;
};

Component350::Component350() : m_impl(new Impl) {}
Component350::~Component350() { delete m_impl; }

float Component350::color() const { return m_impl->m_color; }
float Component350::width() const { return m_impl->m_width; }
void Component350::set_color(float v) { m_impl->m_color = v; }
void Component350::set_opacity(float v) { m_impl->m_opacity = v; }
void Component350::set_width(float v) { m_impl->m_width = v; }
void Component350::set_height(float v) { m_impl->m_height = v; }
void Component350::set_child(Component349* child) { m_impl->m_child = child; }
void Component350::on_click() {}
void Component350::on_hover() {}
void Component350::on_focus() {}
