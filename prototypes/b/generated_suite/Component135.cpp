#include "Component135.h"

#include <cstdint>

struct Component135::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component134* m_child = nullptr;
};

Component135::Component135() :
    m_impl(new Impl) {}
Component135::~Component135() { delete m_impl; }

float Component135::color() const { return m_impl->m_color; }
float Component135::width() const { return m_impl->m_width; }
void Component135::set_color(float v) { m_impl->m_color = v; }
void Component135::set_opacity(float v) { m_impl->m_opacity = v; }
void Component135::set_width(float v) { m_impl->m_width = v; }
void Component135::set_height(float v) { m_impl->m_height = v; }
void Component135::set_child(Component134* child) { m_impl->m_child = child; }
void Component135::on_click() {}
void Component135::on_hover() {}
void Component135::on_focus() {}
