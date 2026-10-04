#include "Component127.h"

#include <cstdint>

struct Component127::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component126* m_child = nullptr;
};

void Component127::set_color(float v) { m_impl->m_color = v; }
void Component127::set_opacity(float v) { m_impl->m_opacity = v; }
void Component127::set_width(float v) { m_impl->m_width = v; }
void Component127::set_height(float v) { m_impl->m_height = v; }
void Component127::set_child(Component126* child) { m_impl->m_child = child; }
void Component127::on_click() {}
void Component127::on_hover() {}
void Component127::on_focus() {}
