#include "Component066.h"

#include <cstdint>

struct Component066::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component065* m_child = nullptr;
};

void Component066::set_color(float v) { m_impl->m_color = v; }
void Component066::set_opacity(float v) { m_impl->m_opacity = v; }
void Component066::set_width(float v) { m_impl->m_width = v; }
void Component066::set_height(float v) { m_impl->m_height = v; }
void Component066::set_child(Component065* child) { m_impl->m_child = child; }
void Component066::on_click() {}
void Component066::on_hover() {}
void Component066::on_focus() {}
