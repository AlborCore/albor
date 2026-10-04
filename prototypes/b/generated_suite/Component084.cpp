#include "Component084.h"

#include <cstdint>

struct Component084::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component083* m_child = nullptr;
};

Component084::Component084() : m_impl(new Impl) {}
Component084::~Component084() { delete m_impl; }

float Component084::color() const { return m_impl->m_color; }
float Component084::width() const { return m_impl->m_width; }
void Component084::set_color(float v) { m_impl->m_color = v; }
void Component084::set_opacity(float v) { m_impl->m_opacity = v; }
void Component084::set_width(float v) { m_impl->m_width = v; }
void Component084::set_height(float v) { m_impl->m_height = v; }
void Component084::set_child(Component083* child) { m_impl->m_child = child; }
void Component084::on_click() {}
void Component084::on_hover() {}
void Component084::on_focus() {}
