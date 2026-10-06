#include "Component227.h"

#include <cstdint>

struct Component227::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component226* m_child = nullptr;
};

Component227::Component227() :
    m_impl(new Impl) {}
Component227::~Component227() { delete m_impl; }

float Component227::color() const { return m_impl->m_color; }
float Component227::width() const { return m_impl->m_width; }
void Component227::set_color(float v) { m_impl->m_color = v; }
void Component227::set_opacity(float v) { m_impl->m_opacity = v; }
void Component227::set_width(float v) { m_impl->m_width = v; }
void Component227::set_height(float v) { m_impl->m_height = v; }
void Component227::set_child(Component226* child) { m_impl->m_child = child; }
void Component227::on_click() {}
void Component227::on_hover() {}
void Component227::on_focus() {}
