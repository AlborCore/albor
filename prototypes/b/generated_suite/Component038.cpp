#include "Component038.h"

#include <cstdint>

struct Component038::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component037* m_child = nullptr;
};

Component038::Component038() :
    m_impl(new Impl) {}
Component038::~Component038() { delete m_impl; }

float Component038::color() const { return m_impl->m_color; }
float Component038::width() const { return m_impl->m_width; }
void Component038::set_color(float v) { m_impl->m_color = v; }
void Component038::set_opacity(float v) { m_impl->m_opacity = v; }
void Component038::set_width(float v) { m_impl->m_width = v; }
void Component038::set_height(float v) { m_impl->m_height = v; }
void Component038::set_child(Component037* child) { m_impl->m_child = child; }
void Component038::on_click() {}
void Component038::on_hover() {}
void Component038::on_focus() {}
