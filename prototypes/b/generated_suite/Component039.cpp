#include "Component039.h"

#include <cstdint>

struct Component039::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component038* m_child = nullptr;
};

Component039::Component039() :
    m_impl(new Impl) {}
Component039::~Component039() { delete m_impl; }

float Component039::color() const { return m_impl->m_color; }
float Component039::width() const { return m_impl->m_width; }
void Component039::set_color(float v) { m_impl->m_color = v; }
void Component039::set_opacity(float v) { m_impl->m_opacity = v; }
void Component039::set_width(float v) { m_impl->m_width = v; }
void Component039::set_height(float v) { m_impl->m_height = v; }
void Component039::set_child(Component038* child) { m_impl->m_child = child; }
void Component039::on_click() {}
void Component039::on_hover() {}
void Component039::on_focus() {}
