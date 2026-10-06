#include "Component187.h"

#include <cstdint>

struct Component187::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component186* m_child = nullptr;
};

Component187::Component187() :
    m_impl(new Impl) {}
Component187::~Component187() { delete m_impl; }

float Component187::color() const { return m_impl->m_color; }
float Component187::width() const { return m_impl->m_width; }
void Component187::set_color(float v) { m_impl->m_color = v; }
void Component187::set_opacity(float v) { m_impl->m_opacity = v; }
void Component187::set_width(float v) { m_impl->m_width = v; }
void Component187::set_height(float v) { m_impl->m_height = v; }
void Component187::set_child(Component186* child) { m_impl->m_child = child; }
void Component187::on_click() {}
void Component187::on_hover() {}
void Component187::on_focus() {}
