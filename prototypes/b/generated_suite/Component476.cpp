#include "Component476.h"

#include <cstdint>

struct Component476::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component475* m_child = nullptr;
};

Component476::Component476() : m_impl(new Impl) {}
Component476::~Component476() { delete m_impl; }

float Component476::color() const { return m_impl->m_color; }
float Component476::width() const { return m_impl->m_width; }
void Component476::set_color(float v) { m_impl->m_color = v; }
void Component476::set_opacity(float v) { m_impl->m_opacity = v; }
void Component476::set_width(float v) { m_impl->m_width = v; }
void Component476::set_height(float v) { m_impl->m_height = v; }
void Component476::set_child(Component475* child) { m_impl->m_child = child; }
void Component476::on_click() {}
void Component476::on_hover() {}
void Component476::on_focus() {}
