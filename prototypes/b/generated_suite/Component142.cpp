#include "Component142.h"

#include <cstdint>

struct Component142::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component141* m_child = nullptr;
};

Component142::Component142() :
    m_impl(new Impl) {}
Component142::~Component142() { delete m_impl; }

float Component142::color() const { return m_impl->m_color; }
float Component142::width() const { return m_impl->m_width; }
void Component142::set_color(float v) { m_impl->m_color = v; }
void Component142::set_opacity(float v) { m_impl->m_opacity = v; }
void Component142::set_width(float v) { m_impl->m_width = v; }
void Component142::set_height(float v) { m_impl->m_height = v; }
void Component142::set_child(Component141* child) { m_impl->m_child = child; }
void Component142::on_click() {}
void Component142::on_hover() {}
void Component142::on_focus() {}
