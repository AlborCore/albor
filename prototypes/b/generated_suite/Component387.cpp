#include "Component387.h"

#include <cstdint>

struct Component387::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component386* m_child = nullptr;
};

Component387::Component387() :
    m_impl(new Impl) {}
Component387::~Component387() { delete m_impl; }

float Component387::color() const { return m_impl->m_color; }
float Component387::width() const { return m_impl->m_width; }
void Component387::set_color(float v) { m_impl->m_color = v; }
void Component387::set_opacity(float v) { m_impl->m_opacity = v; }
void Component387::set_width(float v) { m_impl->m_width = v; }
void Component387::set_height(float v) { m_impl->m_height = v; }
void Component387::set_child(Component386* child) { m_impl->m_child = child; }
void Component387::on_click() {}
void Component387::on_hover() {}
void Component387::on_focus() {}
