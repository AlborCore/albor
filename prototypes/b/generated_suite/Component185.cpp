#include "Component185.h"

#include <cstdint>

struct Component185::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component184* m_child = nullptr;
};

Component185::Component185() :
    m_impl(new Impl) {}
Component185::~Component185() { delete m_impl; }

float Component185::color() const { return m_impl->m_color; }
float Component185::width() const { return m_impl->m_width; }
void Component185::set_color(float v) { m_impl->m_color = v; }
void Component185::set_opacity(float v) { m_impl->m_opacity = v; }
void Component185::set_width(float v) { m_impl->m_width = v; }
void Component185::set_height(float v) { m_impl->m_height = v; }
void Component185::set_child(Component184* child) { m_impl->m_child = child; }
void Component185::on_click() {}
void Component185::on_hover() {}
void Component185::on_focus() {}
