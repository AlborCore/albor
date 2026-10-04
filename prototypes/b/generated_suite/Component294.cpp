#include "Component294.h"

#include <cstdint>

struct Component294::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component293* m_child = nullptr;
};

Component294::Component294() : m_impl(new Impl) {}
Component294::~Component294() { delete m_impl; }

float Component294::color() const { return m_impl->m_color; }
float Component294::width() const { return m_impl->m_width; }
void Component294::set_color(float v) { m_impl->m_color = v; }
void Component294::set_opacity(float v) { m_impl->m_opacity = v; }
void Component294::set_width(float v) { m_impl->m_width = v; }
void Component294::set_height(float v) { m_impl->m_height = v; }
void Component294::set_child(Component293* child) { m_impl->m_child = child; }
void Component294::on_click() {}
void Component294::on_hover() {}
void Component294::on_focus() {}
