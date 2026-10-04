#include "Component369.h"

#include <cstdint>

struct Component369::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component368* m_child = nullptr;
};

Component369::Component369() :
    m_impl(new Impl) {}
Component369::~Component369() { delete m_impl; }

float Component369::color() const { return m_impl->m_color; }
float Component369::width() const { return m_impl->m_width; }
void Component369::set_color(float v) { m_impl->m_color = v; }
void Component369::set_opacity(float v) { m_impl->m_opacity = v; }
void Component369::set_width(float v) { m_impl->m_width = v; }
void Component369::set_height(float v) { m_impl->m_height = v; }
void Component369::set_child(Component368* child) { m_impl->m_child = child; }
void Component369::on_click() {}
void Component369::on_hover() {}
void Component369::on_focus() {}
