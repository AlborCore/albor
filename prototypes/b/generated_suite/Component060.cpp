#include "Component060.h"

#include <cstdint>

struct Component060::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component059* m_child = nullptr;
};

Component060::Component060() :
    m_impl(new Impl) {}
Component060::~Component060() { delete m_impl; }

float Component060::color() const { return m_impl->m_color; }
float Component060::width() const { return m_impl->m_width; }
void Component060::set_color(float v) { m_impl->m_color = v; }
void Component060::set_opacity(float v) { m_impl->m_opacity = v; }
void Component060::set_width(float v) { m_impl->m_width = v; }
void Component060::set_height(float v) { m_impl->m_height = v; }
void Component060::set_child(Component059* child) { m_impl->m_child = child; }
void Component060::on_click() {}
void Component060::on_hover() {}
void Component060::on_focus() {}
