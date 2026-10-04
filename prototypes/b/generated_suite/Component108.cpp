#include "Component108.h"

#include <cstdint>

struct Component108::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component107* m_child = nullptr;
};

Component108::Component108() :
    m_impl(new Impl) {}
Component108::~Component108() { delete m_impl; }

float Component108::color() const { return m_impl->m_color; }
float Component108::width() const { return m_impl->m_width; }
void Component108::set_color(float v) { m_impl->m_color = v; }
void Component108::set_opacity(float v) { m_impl->m_opacity = v; }
void Component108::set_width(float v) { m_impl->m_width = v; }
void Component108::set_height(float v) { m_impl->m_height = v; }
void Component108::set_child(Component107* child) { m_impl->m_child = child; }
void Component108::on_click() {}
void Component108::on_hover() {}
void Component108::on_focus() {}
