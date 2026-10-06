#include "Component184.h"

#include <cstdint>

struct Component184::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component183* m_child = nullptr;
};

Component184::Component184() :
    m_impl(new Impl) {}
Component184::~Component184() { delete m_impl; }

float Component184::color() const { return m_impl->m_color; }
float Component184::width() const { return m_impl->m_width; }
void Component184::set_color(float v) { m_impl->m_color = v; }
void Component184::set_opacity(float v) { m_impl->m_opacity = v; }
void Component184::set_width(float v) { m_impl->m_width = v; }
void Component184::set_height(float v) { m_impl->m_height = v; }
void Component184::set_child(Component183* child) { m_impl->m_child = child; }
void Component184::on_click() {}
void Component184::on_hover() {}
void Component184::on_focus() {}
