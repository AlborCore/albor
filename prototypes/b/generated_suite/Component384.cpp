#include "Component384.h"

#include <cstdint>

struct Component384::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component383* m_child = nullptr;
};

Component384::Component384() :
    m_impl(new Impl) {}
Component384::~Component384() { delete m_impl; }

float Component384::color() const { return m_impl->m_color; }
float Component384::width() const { return m_impl->m_width; }
void Component384::set_color(float v) { m_impl->m_color = v; }
void Component384::set_opacity(float v) { m_impl->m_opacity = v; }
void Component384::set_width(float v) { m_impl->m_width = v; }
void Component384::set_height(float v) { m_impl->m_height = v; }
void Component384::set_child(Component383* child) { m_impl->m_child = child; }
void Component384::on_click() {}
void Component384::on_hover() {}
void Component384::on_focus() {}
