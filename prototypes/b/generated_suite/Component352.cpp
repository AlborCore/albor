#include "Component352.h"

#include <cstdint>

struct Component352::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component351* m_child = nullptr;
};

void Component352::set_color(float v) { m_impl->m_color = v; }
void Component352::set_opacity(float v) { m_impl->m_opacity = v; }
void Component352::set_width(float v) { m_impl->m_width = v; }
void Component352::set_height(float v) { m_impl->m_height = v; }
void Component352::set_child(Component351* child) { m_impl->m_child = child; }
void Component352::on_click() {}
void Component352::on_hover() {}
void Component352::on_focus() {}
