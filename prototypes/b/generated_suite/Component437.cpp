#include "Component437.h"

#include <cstdint>

struct Component437::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component436* m_child = nullptr;
};

void Component437::set_color(float v) { m_impl->m_color = v; }
void Component437::set_opacity(float v) { m_impl->m_opacity = v; }
void Component437::set_width(float v) { m_impl->m_width = v; }
void Component437::set_height(float v) { m_impl->m_height = v; }
void Component437::set_child(Component436* child) { m_impl->m_child = child; }
void Component437::on_click() {}
void Component437::on_hover() {}
void Component437::on_focus() {}
