#include "Component118.h"

#include <cstdint>

struct Component118::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component117* m_child = nullptr;
};

Component118::Component118() : m_impl(new Impl) {}
Component118::~Component118() { delete m_impl; }

float Component118::color() const { return m_impl->m_color; }
float Component118::width() const { return m_impl->m_width; }
void Component118::set_color(float v) { m_impl->m_color = v; }
void Component118::set_opacity(float v) { m_impl->m_opacity = v; }
void Component118::set_width(float v) { m_impl->m_width = v; }
void Component118::set_height(float v) { m_impl->m_height = v; }
void Component118::set_child(Component117* child) { m_impl->m_child = child; }
void Component118::on_click() {}
void Component118::on_hover() {}
void Component118::on_focus() {}
