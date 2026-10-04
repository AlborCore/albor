#include "Component177.h"

#include <cstdint>

struct Component177::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component176* m_child = nullptr;
};

Component177::Component177() :
    m_impl(new Impl) {}
Component177::~Component177() { delete m_impl; }

float Component177::color() const { return m_impl->m_color; }
float Component177::width() const { return m_impl->m_width; }
void Component177::set_color(float v) { m_impl->m_color = v; }
void Component177::set_opacity(float v) { m_impl->m_opacity = v; }
void Component177::set_width(float v) { m_impl->m_width = v; }
void Component177::set_height(float v) { m_impl->m_height = v; }
void Component177::set_child(Component176* child) { m_impl->m_child = child; }
void Component177::on_click() {}
void Component177::on_hover() {}
void Component177::on_focus() {}
