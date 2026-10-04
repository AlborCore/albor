#include "Component007.h"

#include <cstdint>

struct Component007::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component006* m_child = nullptr;
};

Component007::Component007() : m_impl(new Impl) {}
Component007::~Component007() { delete m_impl; }

float Component007::color() const { return m_impl->m_color; }
float Component007::width() const { return m_impl->m_width; }
void Component007::set_color(float v) { m_impl->m_color = v; }
void Component007::set_opacity(float v) { m_impl->m_opacity = v; }
void Component007::set_width(float v) { m_impl->m_width = v; }
void Component007::set_height(float v) { m_impl->m_height = v; }
void Component007::set_child(Component006* child) { m_impl->m_child = child; }
void Component007::on_click() {}
void Component007::on_hover() {}
void Component007::on_focus() {}
