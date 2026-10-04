#include "Component319.h"

#include <cstdint>

struct Component319::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component318* m_child = nullptr;
};

void Component319::set_color(float v) { m_impl->m_color = v; }
void Component319::set_opacity(float v) { m_impl->m_opacity = v; }
void Component319::set_width(float v) { m_impl->m_width = v; }
void Component319::set_height(float v) { m_impl->m_height = v; }
void Component319::set_child(Component318* child) { m_impl->m_child = child; }
void Component319::on_click() {}
void Component319::on_hover() {}
void Component319::on_focus() {}
