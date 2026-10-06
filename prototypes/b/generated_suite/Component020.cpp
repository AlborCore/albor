#include "Component020.h"

#include <cstdint>

struct Component020::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component019* m_child = nullptr;
};

Component020::Component020() :
    m_impl(new Impl) {}
Component020::~Component020() { delete m_impl; }

float Component020::color() const { return m_impl->m_color; }
float Component020::width() const { return m_impl->m_width; }
void Component020::set_color(float v) { m_impl->m_color = v; }
void Component020::set_opacity(float v) { m_impl->m_opacity = v; }
void Component020::set_width(float v) { m_impl->m_width = v; }
void Component020::set_height(float v) { m_impl->m_height = v; }
void Component020::set_child(Component019* child) { m_impl->m_child = child; }
void Component020::on_click() {}
void Component020::on_hover() {}
void Component020::on_focus() {}
