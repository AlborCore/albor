#include "Component052.h"

#include <cstdint>

struct Component052::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component051* m_child = nullptr;
};

Component052::Component052() :
    m_impl(new Impl) {}
Component052::~Component052() { delete m_impl; }

float Component052::color() const { return m_impl->m_color; }
float Component052::width() const { return m_impl->m_width; }
void Component052::set_color(float v) { m_impl->m_color = v; }
void Component052::set_opacity(float v) { m_impl->m_opacity = v; }
void Component052::set_width(float v) { m_impl->m_width = v; }
void Component052::set_height(float v) { m_impl->m_height = v; }
void Component052::set_child(Component051* child) { m_impl->m_child = child; }
void Component052::on_click() {}
void Component052::on_hover() {}
void Component052::on_focus() {}
