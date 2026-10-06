#include "Component000.h"

#include <cstdint>

struct Component000::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
};

Component000::Component000() :
    m_impl(new Impl) {}
Component000::~Component000() { delete m_impl; }

float Component000::color() const { return m_impl->m_color; }
float Component000::width() const { return m_impl->m_width; }
void Component000::set_color(float v) { m_impl->m_color = v; }
void Component000::set_opacity(float v) { m_impl->m_opacity = v; }
void Component000::set_width(float v) { m_impl->m_width = v; }
void Component000::set_height(float v) { m_impl->m_height = v; }
void Component000::on_click() {}
void Component000::on_hover() {}
void Component000::on_focus() {}
