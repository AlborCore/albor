#include "Component011.h"

#include <cstdint>

struct Component011::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component010* m_child = nullptr;
};

Component011::Component011() :
    m_impl(new Impl) {}
Component011::~Component011() { delete m_impl; }

float Component011::color() const { return m_impl->m_color; }
float Component011::width() const { return m_impl->m_width; }
void Component011::set_color(float v) { m_impl->m_color = v; }
void Component011::set_opacity(float v) { m_impl->m_opacity = v; }
void Component011::set_width(float v) { m_impl->m_width = v; }
void Component011::set_height(float v) { m_impl->m_height = v; }
void Component011::set_child(Component010* child) { m_impl->m_child = child; }
void Component011::on_click() {}
void Component011::on_hover() {}
void Component011::on_focus() {}
