#include "Component012.h"

#include <cstdint>

struct Component012::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component011* m_child = nullptr;
};

Component012::Component012() :
    m_impl(new Impl) {}
Component012::~Component012() { delete m_impl; }

float Component012::color() const { return m_impl->m_color; }
float Component012::width() const { return m_impl->m_width; }
void Component012::set_color(float v) { m_impl->m_color = v; }
void Component012::set_opacity(float v) { m_impl->m_opacity = v; }
void Component012::set_width(float v) { m_impl->m_width = v; }
void Component012::set_height(float v) { m_impl->m_height = v; }
void Component012::set_child(Component011* child) { m_impl->m_child = child; }
void Component012::on_click() {}
void Component012::on_hover() {}
void Component012::on_focus() {}
