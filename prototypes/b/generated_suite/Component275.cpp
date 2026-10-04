#include "Component275.h"

#include <cstdint>

struct Component275::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component274* m_child = nullptr;
};

void Component275::set_color(float v) { m_impl->m_color = v; }
void Component275::set_opacity(float v) { m_impl->m_opacity = v; }
void Component275::set_width(float v) { m_impl->m_width = v; }
void Component275::set_height(float v) { m_impl->m_height = v; }
void Component275::set_child(Component274* child) { m_impl->m_child = child; }
void Component275::on_click() {}
void Component275::on_hover() {}
void Component275::on_focus() {}
