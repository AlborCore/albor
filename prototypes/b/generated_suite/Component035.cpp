#include "Component035.h"

#include <cstdint>

struct Component035::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component034* m_child = nullptr;
};

Component035::Component035() : m_impl(new Impl) {}
Component035::~Component035() { delete m_impl; }

float Component035::color() const { return m_impl->m_color; }
float Component035::width() const { return m_impl->m_width; }
void Component035::set_color(float v) { m_impl->m_color = v; }
void Component035::set_opacity(float v) { m_impl->m_opacity = v; }
void Component035::set_width(float v) { m_impl->m_width = v; }
void Component035::set_height(float v) { m_impl->m_height = v; }
void Component035::set_child(Component034* child) { m_impl->m_child = child; }
void Component035::on_click() {}
void Component035::on_hover() {}
void Component035::on_focus() {}
