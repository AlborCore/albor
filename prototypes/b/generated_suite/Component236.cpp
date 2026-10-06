#include "Component236.h"

#include <cstdint>

struct Component236::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component235* m_child = nullptr;
};

Component236::Component236() :
    m_impl(new Impl) {}
Component236::~Component236() { delete m_impl; }

float Component236::color() const { return m_impl->m_color; }
float Component236::width() const { return m_impl->m_width; }
void Component236::set_color(float v) { m_impl->m_color = v; }
void Component236::set_opacity(float v) { m_impl->m_opacity = v; }
void Component236::set_width(float v) { m_impl->m_width = v; }
void Component236::set_height(float v) { m_impl->m_height = v; }
void Component236::set_child(Component235* child) { m_impl->m_child = child; }
void Component236::on_click() {}
void Component236::on_hover() {}
void Component236::on_focus() {}
