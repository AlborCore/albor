#include "Component295.h"

#include <cstdint>

struct Component295::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component294* m_child = nullptr;
};

Component295::Component295() : m_impl(new Impl) {}
Component295::~Component295() { delete m_impl; }

float Component295::color() const { return m_impl->m_color; }
float Component295::width() const { return m_impl->m_width; }
void Component295::set_color(float v) { m_impl->m_color = v; }
void Component295::set_opacity(float v) { m_impl->m_opacity = v; }
void Component295::set_width(float v) { m_impl->m_width = v; }
void Component295::set_height(float v) { m_impl->m_height = v; }
void Component295::set_child(Component294* child) { m_impl->m_child = child; }
void Component295::on_click() {}
void Component295::on_hover() {}
void Component295::on_focus() {}
