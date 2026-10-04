#include "Component157.h"

#include <cstdint>

struct Component157::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component156* m_child = nullptr;
};

Component157::Component157() : m_impl(new Impl) {}
Component157::~Component157() { delete m_impl; }

float Component157::color() const { return m_impl->m_color; }
float Component157::width() const { return m_impl->m_width; }
void Component157::set_color(float v) { m_impl->m_color = v; }
void Component157::set_opacity(float v) { m_impl->m_opacity = v; }
void Component157::set_width(float v) { m_impl->m_width = v; }
void Component157::set_height(float v) { m_impl->m_height = v; }
void Component157::set_child(Component156* child) { m_impl->m_child = child; }
void Component157::on_click() {}
void Component157::on_hover() {}
void Component157::on_focus() {}
