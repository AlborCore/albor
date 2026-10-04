#include "Component196.h"

#include <cstdint>

struct Component196::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component195* m_child = nullptr;
};

Component196::Component196() :
    m_impl(new Impl) {}
Component196::~Component196() { delete m_impl; }

float Component196::color() const { return m_impl->m_color; }
float Component196::width() const { return m_impl->m_width; }
void Component196::set_color(float v) { m_impl->m_color = v; }
void Component196::set_opacity(float v) { m_impl->m_opacity = v; }
void Component196::set_width(float v) { m_impl->m_width = v; }
void Component196::set_height(float v) { m_impl->m_height = v; }
void Component196::set_child(Component195* child) { m_impl->m_child = child; }
void Component196::on_click() {}
void Component196::on_hover() {}
void Component196::on_focus() {}
