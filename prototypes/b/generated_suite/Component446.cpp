#include "Component446.h"

#include <cstdint>

struct Component446::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component445* m_child = nullptr;
};

Component446::Component446() : m_impl(new Impl) {}
Component446::~Component446() { delete m_impl; }

float Component446::color() const { return m_impl->m_color; }
float Component446::width() const { return m_impl->m_width; }
void Component446::set_color(float v) { m_impl->m_color = v; }
void Component446::set_opacity(float v) { m_impl->m_opacity = v; }
void Component446::set_width(float v) { m_impl->m_width = v; }
void Component446::set_height(float v) { m_impl->m_height = v; }
void Component446::set_child(Component445* child) { m_impl->m_child = child; }
void Component446::on_click() {}
void Component446::on_hover() {}
void Component446::on_focus() {}
