#include "Component273.h"

#include <cstdint>

struct Component273::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component272* m_child = nullptr;
};

Component273::Component273() :
    m_impl(new Impl) {}
Component273::~Component273() { delete m_impl; }

float Component273::color() const { return m_impl->m_color; }
float Component273::width() const { return m_impl->m_width; }
void Component273::set_color(float v) { m_impl->m_color = v; }
void Component273::set_opacity(float v) { m_impl->m_opacity = v; }
void Component273::set_width(float v) { m_impl->m_width = v; }
void Component273::set_height(float v) { m_impl->m_height = v; }
void Component273::set_child(Component272* child) { m_impl->m_child = child; }
void Component273::on_click() {}
void Component273::on_hover() {}
void Component273::on_focus() {}
