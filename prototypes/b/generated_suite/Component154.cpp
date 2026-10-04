#include "Component154.h"

#include <cstdint>

struct Component154::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component153* m_child = nullptr;
};

void Component154::set_color(float v) { m_impl->m_color = v; }
void Component154::set_opacity(float v) { m_impl->m_opacity = v; }
void Component154::set_width(float v) { m_impl->m_width = v; }
void Component154::set_height(float v) { m_impl->m_height = v; }
void Component154::set_child(Component153* child) { m_impl->m_child = child; }
void Component154::on_click() {}
void Component154::on_hover() {}
void Component154::on_focus() {}
