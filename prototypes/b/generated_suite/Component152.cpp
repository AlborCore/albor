#include "Component152.h"

#include <cstdint>

struct Component152::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component151* m_child = nullptr;
};

Component152::Component152() : m_impl(new Impl) {}
Component152::~Component152() { delete m_impl; }

float Component152::color() const { return m_impl->m_color; }
float Component152::width() const { return m_impl->m_width; }
void Component152::set_color(float v) { m_impl->m_color = v; }
void Component152::set_opacity(float v) { m_impl->m_opacity = v; }
void Component152::set_width(float v) { m_impl->m_width = v; }
void Component152::set_height(float v) { m_impl->m_height = v; }
void Component152::set_child(Component151* child) { m_impl->m_child = child; }
void Component152::on_click() {}
void Component152::on_hover() {}
void Component152::on_focus() {}
