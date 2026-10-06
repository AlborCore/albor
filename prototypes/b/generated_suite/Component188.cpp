#include "Component188.h"

#include <cstdint>

struct Component188::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component187* m_child = nullptr;
};

Component188::Component188() :
    m_impl(new Impl) {}
Component188::~Component188() { delete m_impl; }

float Component188::color() const { return m_impl->m_color; }
float Component188::width() const { return m_impl->m_width; }
void Component188::set_color(float v) { m_impl->m_color = v; }
void Component188::set_opacity(float v) { m_impl->m_opacity = v; }
void Component188::set_width(float v) { m_impl->m_width = v; }
void Component188::set_height(float v) { m_impl->m_height = v; }
void Component188::set_child(Component187* child) { m_impl->m_child = child; }
void Component188::on_click() {}
void Component188::on_hover() {}
void Component188::on_focus() {}
