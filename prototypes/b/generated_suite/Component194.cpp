#include "Component194.h"

#include <cstdint>

struct Component194::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component193* m_child = nullptr;
};

Component194::Component194() :
    m_impl(new Impl) {}
Component194::~Component194() { delete m_impl; }

float Component194::color() const { return m_impl->m_color; }
float Component194::width() const { return m_impl->m_width; }
void Component194::set_color(float v) { m_impl->m_color = v; }
void Component194::set_opacity(float v) { m_impl->m_opacity = v; }
void Component194::set_width(float v) { m_impl->m_width = v; }
void Component194::set_height(float v) { m_impl->m_height = v; }
void Component194::set_child(Component193* child) { m_impl->m_child = child; }
void Component194::on_click() {}
void Component194::on_hover() {}
void Component194::on_focus() {}
