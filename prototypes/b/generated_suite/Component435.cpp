#include "Component435.h"

#include <cstdint>

struct Component435::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component434* m_child = nullptr;
};

Component435::Component435() :
    m_impl(new Impl) {}
Component435::~Component435() { delete m_impl; }

float Component435::color() const { return m_impl->m_color; }
float Component435::width() const { return m_impl->m_width; }
void Component435::set_color(float v) { m_impl->m_color = v; }
void Component435::set_opacity(float v) { m_impl->m_opacity = v; }
void Component435::set_width(float v) { m_impl->m_width = v; }
void Component435::set_height(float v) { m_impl->m_height = v; }
void Component435::set_child(Component434* child) { m_impl->m_child = child; }
void Component435::on_click() {}
void Component435::on_hover() {}
void Component435::on_focus() {}
