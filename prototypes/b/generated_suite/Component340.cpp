#include "Component340.h"

#include <cstdint>

struct Component340::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component339* m_child = nullptr;
};

Component340::Component340() :
    m_impl(new Impl) {}
Component340::~Component340() { delete m_impl; }

float Component340::color() const { return m_impl->m_color; }
float Component340::width() const { return m_impl->m_width; }
void Component340::set_color(float v) { m_impl->m_color = v; }
void Component340::set_opacity(float v) { m_impl->m_opacity = v; }
void Component340::set_width(float v) { m_impl->m_width = v; }
void Component340::set_height(float v) { m_impl->m_height = v; }
void Component340::set_child(Component339* child) { m_impl->m_child = child; }
void Component340::on_click() {}
void Component340::on_hover() {}
void Component340::on_focus() {}
