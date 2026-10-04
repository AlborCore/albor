#include "Component217.h"

#include <cstdint>

struct Component217::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component216* m_child = nullptr;
};

Component217::Component217() : m_impl(new Impl) {}
Component217::~Component217() { delete m_impl; }

float Component217::color() const { return m_impl->m_color; }
float Component217::width() const { return m_impl->m_width; }
void Component217::set_color(float v) { m_impl->m_color = v; }
void Component217::set_opacity(float v) { m_impl->m_opacity = v; }
void Component217::set_width(float v) { m_impl->m_width = v; }
void Component217::set_height(float v) { m_impl->m_height = v; }
void Component217::set_child(Component216* child) { m_impl->m_child = child; }
void Component217::on_click() {}
void Component217::on_hover() {}
void Component217::on_focus() {}
