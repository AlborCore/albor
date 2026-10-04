#include "Component230.h"

#include <cstdint>

struct Component230::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component229* m_child = nullptr;
};

Component230::Component230() : m_impl(new Impl) {}
Component230::~Component230() { delete m_impl; }

float Component230::color() const { return m_impl->m_color; }
float Component230::width() const { return m_impl->m_width; }
void Component230::set_color(float v) { m_impl->m_color = v; }
void Component230::set_opacity(float v) { m_impl->m_opacity = v; }
void Component230::set_width(float v) { m_impl->m_width = v; }
void Component230::set_height(float v) { m_impl->m_height = v; }
void Component230::set_child(Component229* child) { m_impl->m_child = child; }
void Component230::on_click() {}
void Component230::on_hover() {}
void Component230::on_focus() {}
