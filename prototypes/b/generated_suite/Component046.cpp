#include "Component046.h"

#include <cstdint>

struct Component046::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component045* m_child = nullptr;
};

Component046::Component046() :
    m_impl(new Impl) {}
Component046::~Component046() { delete m_impl; }

float Component046::color() const { return m_impl->m_color; }
float Component046::width() const { return m_impl->m_width; }
void Component046::set_color(float v) { m_impl->m_color = v; }
void Component046::set_opacity(float v) { m_impl->m_opacity = v; }
void Component046::set_width(float v) { m_impl->m_width = v; }
void Component046::set_height(float v) { m_impl->m_height = v; }
void Component046::set_child(Component045* child) { m_impl->m_child = child; }
void Component046::on_click() {}
void Component046::on_hover() {}
void Component046::on_focus() {}
