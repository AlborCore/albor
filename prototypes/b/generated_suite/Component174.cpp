#include "Component174.h"

#include <cstdint>

struct Component174::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component173* m_child = nullptr;
};

Component174::Component174() :
    m_impl(new Impl) {}
Component174::~Component174() { delete m_impl; }

float Component174::color() const { return m_impl->m_color; }
float Component174::width() const { return m_impl->m_width; }
void Component174::set_color(float v) { m_impl->m_color = v; }
void Component174::set_opacity(float v) { m_impl->m_opacity = v; }
void Component174::set_width(float v) { m_impl->m_width = v; }
void Component174::set_height(float v) { m_impl->m_height = v; }
void Component174::set_child(Component173* child) { m_impl->m_child = child; }
void Component174::on_click() {}
void Component174::on_hover() {}
void Component174::on_focus() {}
