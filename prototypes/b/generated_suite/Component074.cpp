#include "Component074.h"

#include <cstdint>

struct Component074::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component073* m_child = nullptr;
};

Component074::Component074() :
    m_impl(new Impl) {}
Component074::~Component074() { delete m_impl; }

float Component074::color() const { return m_impl->m_color; }
float Component074::width() const { return m_impl->m_width; }
void Component074::set_color(float v) { m_impl->m_color = v; }
void Component074::set_opacity(float v) { m_impl->m_opacity = v; }
void Component074::set_width(float v) { m_impl->m_width = v; }
void Component074::set_height(float v) { m_impl->m_height = v; }
void Component074::set_child(Component073* child) { m_impl->m_child = child; }
void Component074::on_click() {}
void Component074::on_hover() {}
void Component074::on_focus() {}
