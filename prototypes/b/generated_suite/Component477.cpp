#include "Component477.h"

#include <cstdint>

struct Component477::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component476* m_child = nullptr;
};

Component477::Component477() :
    m_impl(new Impl) {}
Component477::~Component477() { delete m_impl; }

float Component477::color() const { return m_impl->m_color; }
float Component477::width() const { return m_impl->m_width; }
void Component477::set_color(float v) { m_impl->m_color = v; }
void Component477::set_opacity(float v) { m_impl->m_opacity = v; }
void Component477::set_width(float v) { m_impl->m_width = v; }
void Component477::set_height(float v) { m_impl->m_height = v; }
void Component477::set_child(Component476* child) { m_impl->m_child = child; }
void Component477::on_click() {}
void Component477::on_hover() {}
void Component477::on_focus() {}
