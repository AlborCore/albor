#include "Component044.h"

#include <cstdint>

struct Component044::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component043* m_child = nullptr;
};

Component044::Component044() :
    m_impl(new Impl) {}
Component044::~Component044() { delete m_impl; }

float Component044::color() const { return m_impl->m_color; }
float Component044::width() const { return m_impl->m_width; }
void Component044::set_color(float v) { m_impl->m_color = v; }
void Component044::set_opacity(float v) { m_impl->m_opacity = v; }
void Component044::set_width(float v) { m_impl->m_width = v; }
void Component044::set_height(float v) { m_impl->m_height = v; }
void Component044::set_child(Component043* child) { m_impl->m_child = child; }
void Component044::on_click() {}
void Component044::on_hover() {}
void Component044::on_focus() {}
