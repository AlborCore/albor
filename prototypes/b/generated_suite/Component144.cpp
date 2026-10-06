#include "Component144.h"

#include <cstdint>

struct Component144::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component143* m_child = nullptr;
};

Component144::Component144() :
    m_impl(new Impl) {}
Component144::~Component144() { delete m_impl; }

float Component144::color() const { return m_impl->m_color; }
float Component144::width() const { return m_impl->m_width; }
void Component144::set_color(float v) { m_impl->m_color = v; }
void Component144::set_opacity(float v) { m_impl->m_opacity = v; }
void Component144::set_width(float v) { m_impl->m_width = v; }
void Component144::set_height(float v) { m_impl->m_height = v; }
void Component144::set_child(Component143* child) { m_impl->m_child = child; }
void Component144::on_click() {}
void Component144::on_hover() {}
void Component144::on_focus() {}
