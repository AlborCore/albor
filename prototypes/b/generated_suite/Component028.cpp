#include "Component028.h"

#include <cstdint>

struct Component028::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component027* m_child = nullptr;
};

Component028::Component028() : m_impl(new Impl) {}
Component028::~Component028() { delete m_impl; }

float Component028::color() const { return m_impl->m_color; }
float Component028::width() const { return m_impl->m_width; }
void Component028::set_color(float v) { m_impl->m_color = v; }
void Component028::set_opacity(float v) { m_impl->m_opacity = v; }
void Component028::set_width(float v) { m_impl->m_width = v; }
void Component028::set_height(float v) { m_impl->m_height = v; }
void Component028::set_child(Component027* child) { m_impl->m_child = child; }
void Component028::on_click() {}
void Component028::on_hover() {}
void Component028::on_focus() {}
