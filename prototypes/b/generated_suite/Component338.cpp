#include "Component338.h"

#include <cstdint>

struct Component338::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component337* m_child = nullptr;
};

Component338::Component338() : m_impl(new Impl) {}
Component338::~Component338() { delete m_impl; }

float Component338::color() const { return m_impl->m_color; }
float Component338::width() const { return m_impl->m_width; }
void Component338::set_color(float v) { m_impl->m_color = v; }
void Component338::set_opacity(float v) { m_impl->m_opacity = v; }
void Component338::set_width(float v) { m_impl->m_width = v; }
void Component338::set_height(float v) { m_impl->m_height = v; }
void Component338::set_child(Component337* child) { m_impl->m_child = child; }
void Component338::on_click() {}
void Component338::on_hover() {}
void Component338::on_focus() {}
