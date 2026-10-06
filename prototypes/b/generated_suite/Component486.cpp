#include "Component486.h"

#include <cstdint>

struct Component486::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component485* m_child = nullptr;
};

Component486::Component486() :
    m_impl(new Impl) {}
Component486::~Component486() { delete m_impl; }

float Component486::color() const { return m_impl->m_color; }
float Component486::width() const { return m_impl->m_width; }
void Component486::set_color(float v) { m_impl->m_color = v; }
void Component486::set_opacity(float v) { m_impl->m_opacity = v; }
void Component486::set_width(float v) { m_impl->m_width = v; }
void Component486::set_height(float v) { m_impl->m_height = v; }
void Component486::set_child(Component485* child) { m_impl->m_child = child; }
void Component486::on_click() {}
void Component486::on_hover() {}
void Component486::on_focus() {}
