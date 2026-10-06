#include "Component386.h"

#include <cstdint>

struct Component386::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component385* m_child = nullptr;
};

Component386::Component386() :
    m_impl(new Impl) {}
Component386::~Component386() { delete m_impl; }

float Component386::color() const { return m_impl->m_color; }
float Component386::width() const { return m_impl->m_width; }
void Component386::set_color(float v) { m_impl->m_color = v; }
void Component386::set_opacity(float v) { m_impl->m_opacity = v; }
void Component386::set_width(float v) { m_impl->m_width = v; }
void Component386::set_height(float v) { m_impl->m_height = v; }
void Component386::set_child(Component385* child) { m_impl->m_child = child; }
void Component386::on_click() {}
void Component386::on_hover() {}
void Component386::on_focus() {}
