#include "Component358.h"

#include <cstdint>

struct Component358::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component357* m_child = nullptr;
};

Component358::Component358() :
    m_impl(new Impl) {}
Component358::~Component358() { delete m_impl; }

float Component358::color() const { return m_impl->m_color; }
float Component358::width() const { return m_impl->m_width; }
void Component358::set_color(float v) { m_impl->m_color = v; }
void Component358::set_opacity(float v) { m_impl->m_opacity = v; }
void Component358::set_width(float v) { m_impl->m_width = v; }
void Component358::set_height(float v) { m_impl->m_height = v; }
void Component358::set_child(Component357* child) { m_impl->m_child = child; }
void Component358::on_click() {}
void Component358::on_hover() {}
void Component358::on_focus() {}
