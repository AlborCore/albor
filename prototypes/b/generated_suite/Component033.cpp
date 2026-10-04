#include "Component033.h"

#include <cstdint>

struct Component033::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component032* m_child = nullptr;
};

Component033::Component033() : m_impl(new Impl) {}
Component033::~Component033() { delete m_impl; }

float Component033::color() const { return m_impl->m_color; }
float Component033::width() const { return m_impl->m_width; }
void Component033::set_color(float v) { m_impl->m_color = v; }
void Component033::set_opacity(float v) { m_impl->m_opacity = v; }
void Component033::set_width(float v) { m_impl->m_width = v; }
void Component033::set_height(float v) { m_impl->m_height = v; }
void Component033::set_child(Component032* child) { m_impl->m_child = child; }
void Component033::on_click() {}
void Component033::on_hover() {}
void Component033::on_focus() {}
