#include "Component493.h"

#include <cstdint>

struct Component493::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component492* m_child = nullptr;
};

Component493::Component493() :
    m_impl(new Impl) {}
Component493::~Component493() { delete m_impl; }

float Component493::color() const { return m_impl->m_color; }
float Component493::width() const { return m_impl->m_width; }
void Component493::set_color(float v) { m_impl->m_color = v; }
void Component493::set_opacity(float v) { m_impl->m_opacity = v; }
void Component493::set_width(float v) { m_impl->m_width = v; }
void Component493::set_height(float v) { m_impl->m_height = v; }
void Component493::set_child(Component492* child) { m_impl->m_child = child; }
void Component493::on_click() {}
void Component493::on_hover() {}
void Component493::on_focus() {}
