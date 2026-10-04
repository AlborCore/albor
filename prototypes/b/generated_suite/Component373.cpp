#include "Component373.h"

#include <cstdint>

struct Component373::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component372* m_child = nullptr;
};

Component373::Component373() : m_impl(new Impl) {}
Component373::~Component373() { delete m_impl; }

float Component373::color() const { return m_impl->m_color; }
float Component373::width() const { return m_impl->m_width; }
void Component373::set_color(float v) { m_impl->m_color = v; }
void Component373::set_opacity(float v) { m_impl->m_opacity = v; }
void Component373::set_width(float v) { m_impl->m_width = v; }
void Component373::set_height(float v) { m_impl->m_height = v; }
void Component373::set_child(Component372* child) { m_impl->m_child = child; }
void Component373::on_click() {}
void Component373::on_hover() {}
void Component373::on_focus() {}
