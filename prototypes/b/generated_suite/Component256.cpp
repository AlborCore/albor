#include "Component256.h"

#include <cstdint>

struct Component256::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component255* m_child = nullptr;
};

Component256::Component256() :
    m_impl(new Impl) {}
Component256::~Component256() { delete m_impl; }

float Component256::color() const { return m_impl->m_color; }
float Component256::width() const { return m_impl->m_width; }
void Component256::set_color(float v) { m_impl->m_color = v; }
void Component256::set_opacity(float v) { m_impl->m_opacity = v; }
void Component256::set_width(float v) { m_impl->m_width = v; }
void Component256::set_height(float v) { m_impl->m_height = v; }
void Component256::set_child(Component255* child) { m_impl->m_child = child; }
void Component256::on_click() {}
void Component256::on_hover() {}
void Component256::on_focus() {}
