#include "Component094.h"

#include <cstdint>

struct Component094::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component093* m_child = nullptr;
};

Component094::Component094() :
    m_impl(new Impl) {}
Component094::~Component094() { delete m_impl; }

float Component094::color() const { return m_impl->m_color; }
float Component094::width() const { return m_impl->m_width; }
void Component094::set_color(float v) { m_impl->m_color = v; }
void Component094::set_opacity(float v) { m_impl->m_opacity = v; }
void Component094::set_width(float v) { m_impl->m_width = v; }
void Component094::set_height(float v) { m_impl->m_height = v; }
void Component094::set_child(Component093* child) { m_impl->m_child = child; }
void Component094::on_click() {}
void Component094::on_hover() {}
void Component094::on_focus() {}
