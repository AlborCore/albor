#include "Component498.h"

#include <cstdint>

struct Component498::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component497* m_child = nullptr;
};

Component498::Component498() :
    m_impl(new Impl) {}
Component498::~Component498() { delete m_impl; }

float Component498::color() const { return m_impl->m_color; }
float Component498::width() const { return m_impl->m_width; }
void Component498::set_color(float v) { m_impl->m_color = v; }
void Component498::set_opacity(float v) { m_impl->m_opacity = v; }
void Component498::set_width(float v) { m_impl->m_width = v; }
void Component498::set_height(float v) { m_impl->m_height = v; }
void Component498::set_child(Component497* child) { m_impl->m_child = child; }
void Component498::on_click() {}
void Component498::on_hover() {}
void Component498::on_focus() {}
