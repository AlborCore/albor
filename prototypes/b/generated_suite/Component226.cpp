#include "Component226.h"

#include <cstdint>

struct Component226::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component225* m_child = nullptr;
};

void Component226::set_color(float v) { m_impl->m_color = v; }
void Component226::set_opacity(float v) { m_impl->m_opacity = v; }
void Component226::set_width(float v) { m_impl->m_width = v; }
void Component226::set_height(float v) { m_impl->m_height = v; }
void Component226::set_child(Component225* child) { m_impl->m_child = child; }
void Component226::on_click() {}
void Component226::on_hover() {}
void Component226::on_focus() {}
