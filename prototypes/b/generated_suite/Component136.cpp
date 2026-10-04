#include "Component136.h"

#include <cstdint>

struct Component136::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component135* m_child = nullptr;
};

void Component136::set_color(float v) { m_impl->m_color = v; }
void Component136::set_opacity(float v) { m_impl->m_opacity = v; }
void Component136::set_width(float v) { m_impl->m_width = v; }
void Component136::set_height(float v) { m_impl->m_height = v; }
void Component136::set_child(Component135* child) { m_impl->m_child = child; }
void Component136::on_click() {}
void Component136::on_hover() {}
void Component136::on_focus() {}
