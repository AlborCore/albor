#include "Component451.h"

#include <cstdint>

struct Component451::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component450* m_child = nullptr;
};

Component451::Component451() : m_impl(new Impl) {}
Component451::~Component451() { delete m_impl; }

float Component451::color() const { return m_impl->m_color; }
float Component451::width() const { return m_impl->m_width; }
void Component451::set_color(float v) { m_impl->m_color = v; }
void Component451::set_opacity(float v) { m_impl->m_opacity = v; }
void Component451::set_width(float v) { m_impl->m_width = v; }
void Component451::set_height(float v) { m_impl->m_height = v; }
void Component451::set_child(Component450* child) { m_impl->m_child = child; }
void Component451::on_click() {}
void Component451::on_hover() {}
void Component451::on_focus() {}
