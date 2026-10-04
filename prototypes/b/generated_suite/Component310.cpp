#include "Component310.h"

#include <cstdint>

struct Component310::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component309* m_child = nullptr;
};

void Component310::set_color(float v) { m_impl->m_color = v; }
void Component310::set_opacity(float v) { m_impl->m_opacity = v; }
void Component310::set_width(float v) { m_impl->m_width = v; }
void Component310::set_height(float v) { m_impl->m_height = v; }
void Component310::set_child(Component309* child) { m_impl->m_child = child; }
void Component310::on_click() {}
void Component310::on_hover() {}
void Component310::on_focus() {}
