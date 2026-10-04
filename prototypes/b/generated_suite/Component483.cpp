#include "Component483.h"

#include <cstdint>

struct Component483::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component482* m_child = nullptr;
};

Component483::Component483() : m_impl(new Impl) {}
Component483::~Component483() { delete m_impl; }

float Component483::color() const { return m_impl->m_color; }
float Component483::width() const { return m_impl->m_width; }
void Component483::set_color(float v) { m_impl->m_color = v; }
void Component483::set_opacity(float v) { m_impl->m_opacity = v; }
void Component483::set_width(float v) { m_impl->m_width = v; }
void Component483::set_height(float v) { m_impl->m_height = v; }
void Component483::set_child(Component482* child) { m_impl->m_child = child; }
void Component483::on_click() {}
void Component483::on_hover() {}
void Component483::on_focus() {}
