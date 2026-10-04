#include "Component417.h"

#include <cstdint>

struct Component417::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component416* m_child = nullptr;
};

Component417::Component417() : m_impl(new Impl) {}
Component417::~Component417() { delete m_impl; }

float Component417::color() const { return m_impl->m_color; }
float Component417::width() const { return m_impl->m_width; }
void Component417::set_color(float v) { m_impl->m_color = v; }
void Component417::set_opacity(float v) { m_impl->m_opacity = v; }
void Component417::set_width(float v) { m_impl->m_width = v; }
void Component417::set_height(float v) { m_impl->m_height = v; }
void Component417::set_child(Component416* child) { m_impl->m_child = child; }
void Component417::on_click() {}
void Component417::on_hover() {}
void Component417::on_focus() {}
