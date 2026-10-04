#include "Component314.h"

#include <cstdint>

struct Component314::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component313* m_child = nullptr;
};

Component314::Component314() :
    m_impl(new Impl) {}
Component314::~Component314() { delete m_impl; }

float Component314::color() const { return m_impl->m_color; }
float Component314::width() const { return m_impl->m_width; }
void Component314::set_color(float v) { m_impl->m_color = v; }
void Component314::set_opacity(float v) { m_impl->m_opacity = v; }
void Component314::set_width(float v) { m_impl->m_width = v; }
void Component314::set_height(float v) { m_impl->m_height = v; }
void Component314::set_child(Component313* child) { m_impl->m_child = child; }
void Component314::on_click() {}
void Component314::on_hover() {}
void Component314::on_focus() {}
