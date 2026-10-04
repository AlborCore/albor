#include "Component402.h"

#include <cstdint>

struct Component402::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component401* m_child = nullptr;
};

Component402::Component402() : m_impl(new Impl) {}
Component402::~Component402() { delete m_impl; }

float Component402::color() const { return m_impl->m_color; }
float Component402::width() const { return m_impl->m_width; }
void Component402::set_color(float v) { m_impl->m_color = v; }
void Component402::set_opacity(float v) { m_impl->m_opacity = v; }
void Component402::set_width(float v) { m_impl->m_width = v; }
void Component402::set_height(float v) { m_impl->m_height = v; }
void Component402::set_child(Component401* child) { m_impl->m_child = child; }
void Component402::on_click() {}
void Component402::on_hover() {}
void Component402::on_focus() {}
