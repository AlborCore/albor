#include "Component021.h"

#include <cstdint>

struct Component021::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component020* m_child = nullptr;
};

Component021::Component021() :
    m_impl(new Impl) {}
Component021::~Component021() { delete m_impl; }

float Component021::color() const { return m_impl->m_color; }
float Component021::width() const { return m_impl->m_width; }
void Component021::set_color(float v) { m_impl->m_color = v; }
void Component021::set_opacity(float v) { m_impl->m_opacity = v; }
void Component021::set_width(float v) { m_impl->m_width = v; }
void Component021::set_height(float v) { m_impl->m_height = v; }
void Component021::set_child(Component020* child) { m_impl->m_child = child; }
void Component021::on_click() {}
void Component021::on_hover() {}
void Component021::on_focus() {}
