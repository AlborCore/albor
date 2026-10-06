#include "Component176.h"

#include <cstdint>

struct Component176::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component175* m_child = nullptr;
};

Component176::Component176() :
    m_impl(new Impl) {}
Component176::~Component176() { delete m_impl; }

float Component176::color() const { return m_impl->m_color; }
float Component176::width() const { return m_impl->m_width; }
void Component176::set_color(float v) { m_impl->m_color = v; }
void Component176::set_opacity(float v) { m_impl->m_opacity = v; }
void Component176::set_width(float v) { m_impl->m_width = v; }
void Component176::set_height(float v) { m_impl->m_height = v; }
void Component176::set_child(Component175* child) { m_impl->m_child = child; }
void Component176::on_click() {}
void Component176::on_hover() {}
void Component176::on_focus() {}
