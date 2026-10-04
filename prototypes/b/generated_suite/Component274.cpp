#include "Component274.h"

#include <cstdint>

struct Component274::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component273* m_child = nullptr;
};

void Component274::set_color(float v) { m_impl->m_color = v; }
void Component274::set_opacity(float v) { m_impl->m_opacity = v; }
void Component274::set_width(float v) { m_impl->m_width = v; }
void Component274::set_height(float v) { m_impl->m_height = v; }
void Component274::set_child(Component273* child) { m_impl->m_child = child; }
void Component274::on_click() {}
void Component274::on_hover() {}
void Component274::on_focus() {}
