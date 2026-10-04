#include "Component467.h"

#include <cstdint>

struct Component467::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component466* m_child = nullptr;
};

void Component467::set_color(float v) { m_impl->m_color = v; }
void Component467::set_opacity(float v) { m_impl->m_opacity = v; }
void Component467::set_width(float v) { m_impl->m_width = v; }
void Component467::set_height(float v) { m_impl->m_height = v; }
void Component467::set_child(Component466* child) { m_impl->m_child = child; }
void Component467::on_click() {}
void Component467::on_hover() {}
void Component467::on_focus() {}
