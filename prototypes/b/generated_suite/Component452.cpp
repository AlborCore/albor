#include "Component452.h"

#include <cstdint>

struct Component452::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component451* m_child = nullptr;
};

void Component452::set_color(float v) { m_impl->m_color = v; }
void Component452::set_opacity(float v) { m_impl->m_opacity = v; }
void Component452::set_width(float v) { m_impl->m_width = v; }
void Component452::set_height(float v) { m_impl->m_height = v; }
void Component452::set_child(Component451* child) { m_impl->m_child = child; }
void Component452::on_click() {}
void Component452::on_hover() {}
void Component452::on_focus() {}
