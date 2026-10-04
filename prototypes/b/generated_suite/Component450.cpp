#include "Component450.h"

#include <cstdint>

struct Component450::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component449* m_child = nullptr;
};

Component450::Component450() : m_impl(new Impl) {}
Component450::~Component450() { delete m_impl; }

float Component450::color() const { return m_impl->m_color; }
float Component450::width() const { return m_impl->m_width; }
void Component450::set_color(float v) { m_impl->m_color = v; }
void Component450::set_opacity(float v) { m_impl->m_opacity = v; }
void Component450::set_width(float v) { m_impl->m_width = v; }
void Component450::set_height(float v) { m_impl->m_height = v; }
void Component450::set_child(Component449* child) { m_impl->m_child = child; }
void Component450::on_click() {}
void Component450::on_hover() {}
void Component450::on_focus() {}
