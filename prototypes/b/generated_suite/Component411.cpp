#include "Component411.h"

#include <cstdint>

struct Component411::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component410* m_child = nullptr;
};

Component411::Component411() :
    m_impl(new Impl) {}
Component411::~Component411() { delete m_impl; }

float Component411::color() const { return m_impl->m_color; }
float Component411::width() const { return m_impl->m_width; }
void Component411::set_color(float v) { m_impl->m_color = v; }
void Component411::set_opacity(float v) { m_impl->m_opacity = v; }
void Component411::set_width(float v) { m_impl->m_width = v; }
void Component411::set_height(float v) { m_impl->m_height = v; }
void Component411::set_child(Component410* child) { m_impl->m_child = child; }
void Component411::on_click() {}
void Component411::on_hover() {}
void Component411::on_focus() {}
