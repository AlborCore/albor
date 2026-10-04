#include "Component042.h"

#include <cstdint>

struct Component042::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component041* m_child = nullptr;
};

Component042::Component042() :
    m_impl(new Impl) {}
Component042::~Component042() { delete m_impl; }

float Component042::color() const { return m_impl->m_color; }
float Component042::width() const { return m_impl->m_width; }
void Component042::set_color(float v) { m_impl->m_color = v; }
void Component042::set_opacity(float v) { m_impl->m_opacity = v; }
void Component042::set_width(float v) { m_impl->m_width = v; }
void Component042::set_height(float v) { m_impl->m_height = v; }
void Component042::set_child(Component041* child) { m_impl->m_child = child; }
void Component042::on_click() {}
void Component042::on_hover() {}
void Component042::on_focus() {}
