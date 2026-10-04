#include "Component147.h"

#include <cstdint>

struct Component147::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component146* m_child = nullptr;
};

Component147::Component147() :
    m_impl(new Impl) {}
Component147::~Component147() { delete m_impl; }

float Component147::color() const { return m_impl->m_color; }
float Component147::width() const { return m_impl->m_width; }
void Component147::set_color(float v) { m_impl->m_color = v; }
void Component147::set_opacity(float v) { m_impl->m_opacity = v; }
void Component147::set_width(float v) { m_impl->m_width = v; }
void Component147::set_height(float v) { m_impl->m_height = v; }
void Component147::set_child(Component146* child) { m_impl->m_child = child; }
void Component147::on_click() {}
void Component147::on_hover() {}
void Component147::on_focus() {}
