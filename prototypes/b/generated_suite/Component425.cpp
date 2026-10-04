#include "Component425.h"

#include <cstdint>

struct Component425::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component424* m_child = nullptr;
};

Component425::Component425() : m_impl(new Impl) {}
Component425::~Component425() { delete m_impl; }

float Component425::color() const { return m_impl->m_color; }
float Component425::width() const { return m_impl->m_width; }
void Component425::set_color(float v) { m_impl->m_color = v; }
void Component425::set_opacity(float v) { m_impl->m_opacity = v; }
void Component425::set_width(float v) { m_impl->m_width = v; }
void Component425::set_height(float v) { m_impl->m_height = v; }
void Component425::set_child(Component424* child) { m_impl->m_child = child; }
void Component425::on_click() {}
void Component425::on_hover() {}
void Component425::on_focus() {}
